#include "interpreter.h"

#include "attr.h"
#include "builtin_function.h"
#include "class_object.h"
#include "code_object.h"
#include "code_object_print.h"
#include "dict.h"
#include "function.h"
#include "instance.h"
#include "list.h"
#include "range_iterator.h"
#include "subscript.h"
#include "thread_state.h"
#include "value.h"
#include "virtual_machine.h"
#include <fmt/core.h>

namespace cl
{

#define PARAMS                                                                 \
    Value *fp, const uint8_t *pc, Value accumulator, void *dispatch,           \
        CodeObject *code_object
#define ARGS fp, pc, accumulator, dispatch, code_object

    using DispatchTableEntry = Value (*)(PARAMS);

    struct DispatchTable
    {
        DispatchTableEntry table[BytecodeTableSize];
    };

#define START(len)                                                             \
    static constexpr uint32_t instr_len = len;                                 \
    auto *dispatch_fun =                                                       \
        reinterpret_cast<DispatchTable *>(dispatch)->table[pc[instr_len]]
#define COMPLETE()                                                             \
    pc += instr_len;                                                           \
    MUSTTAIL return dispatch_fun(ARGS)

#define START_BINARY_REG_ACC()                                                 \
    START(2);                                                                  \
    int8_t reg = pc[1];                                                        \
    Value a = fp[reg];                                                         \
    Value b = accumulator

#define START_BINARY_ACC_SMI()                                                 \
    START(2);                                                                  \
    Value a = accumulator;                                                     \
    Value b = Value::from_smi(int8_t(pc[1]))

#define START_UNARY_ACC()                                                      \
    START(1);                                                                  \
    Value a = accumulator

#define A_NOT_SMI() ((a.as.integer & value_not_smi_mask) != 0)
#define A_OR_B_NOT_SMI()                                                       \
    (((a.as.integer | b.as.integer) & value_not_smi_mask) != 0)
#define A_OR_B_REFCOUNTED_PTR()                                                \
    (((a.as.integer | b.as.integer) & value_refcounted_ptr_tag) != 0)

    static uint32_t read_uint32_le(const uint8_t *p)
    {
        return (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    }

    static int16_t read_int16_le(const uint8_t *p)
    {
#if 1
        const int16_t *ptr = reinterpret_cast<const int16_t *>(p);
        return *ptr;
#else
        return (p[0] << 0) | (p[1] << 8);
#endif
    }

    NOINLINE Value raise_generic_exception(PARAMS)
    {
        throw std::runtime_error("Clovervm exception");
    }

    NOINLINE Value raise_unknown_opcode_exception(PARAMS)
    {
        throw std::runtime_error("Unknown opcode");
    }

    NOINLINE Value raise_value_error_negative_shift_count(PARAMS)
    {
        throw std::runtime_error("ValueError: negative shift count");
    }

    NOINLINE Value name_error(PARAMS) { throw std::runtime_error("NameError"); }

    NOINLINE Value not_callable_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not callable");
    }

    NOINLINE Value attribute_error(PARAMS)
    {
        throw std::runtime_error("AttributeError");
    }

    NOINLINE Value attribute_assignment_error(PARAMS)
    {
        throw std::runtime_error("AttributeError: cannot assign attribute");
    }

    NOINLINE Value subscript_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not subscriptable");
    }

    NOINLINE Value method_lookup_error(PARAMS)
    {
        throw std::runtime_error("AttributeError");
    }

    NOINLINE Value wrong_arity_error(PARAMS)
    {
        throw std::runtime_error("TypeError: wrong number of arguments");
    }

    static constexpr uint32_t kDefaultInstanceInlineSlotCount = 4;

    static ALWAYSINLINE void
    initialize_frame_header(Value *new_fp, Value *previous_fp,
                            CodeObject *return_code_object,
                            const uint8_t *return_pc)
    {
        // these aren't really values. we're just going to whack them in and
        // ask the refcounter to ignore them.
        new_fp[0].as.ptr = (Object *)previous_fp;
        new_fp[1] = Value::from_oop(return_code_object);
        new_fp[-1].as.ptr = (Object *)return_pc;
    }

    static ALWAYSINLINE void restore_frame_header(Value *&fp,
                                                  const uint8_t *&pc,
                                                  CodeObject *&code_object)
    {
        pc = (const uint8_t *)fp[-1].as.ptr;
        code_object = fp[1].get_ptr<CodeObject>();
        fp = (Value *)fp[0].as.ptr;
    }

    static ALWAYSINLINE int32_t frame_positive_slot_count(CodeObject *code_obj)
    {
        return FrameHeaderSizeAboveFp + int32_t(code_obj->n_locals) +
               int32_t(code_obj->n_temporaries);
    }

    static Value *make_nested_frame(Value *fp, CodeObject *caller_code_object,
                                    const uint8_t *return_pc,
                                    CodeObject *return_code_object)
    {
        Value *new_fp = fp + frame_positive_slot_count(caller_code_object) +
                        FrameHeaderSizeBelowFp;
        initialize_frame_header(new_fp, fp, return_code_object, return_pc);
        return new_fp;
    }

    static void initialize_class_body_frame(Value *fp, CodeObject *body_code)
    {
        Scope *local_scope = body_code->get_local_scope_ptr();
        for(uint32_t slot_idx = 0; slot_idx < local_scope->size(); ++slot_idx)
        {
            if(!local_scope->slot_is_named(slot_idx))
            {
                continue;
            }
            fp[body_code->encode_reg(slot_idx)] =
                local_scope->get_by_slot_index_fastpath_only(slot_idx);
        }
    }

    static Value build_class_from_frame(Value *fp, CodeObject *body_code,
                                        TValue<String> class_name)
    {
        TValue<ClassObject> cls =
            ThreadState::get_active()->make_refcounted_value<ClassObject>(
                class_name, kDefaultInstanceInlineSlotCount);
        Scope *local_scope = body_code->get_local_scope_ptr();
        for(uint32_t slot_idx = 0; slot_idx < local_scope->size(); ++slot_idx)
        {
            if(!local_scope->slot_is_named(slot_idx))
            {
                continue;
            }

            Value value = fp[body_code->encode_reg(slot_idx)];
            if(value.is_not_present())
            {
                continue;
            }
            cls.extract()->set_member(
                local_scope->get_name_by_slot_index(slot_idx), value);
        }
        return Value::from_oop(cls.extract());
    }

    NOINLINE Value not_iterable_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not iterable");
    }

    NOINLINE Value not_iterator_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not an iterator");
    }

    NOINLINE Value range_integer_argument_error(PARAMS)
    {
        throw std::runtime_error(
            "TypeError: range() arguments must be integers");
    }

    NOINLINE Value range_zero_step_error(PARAMS)
    {
        throw std::runtime_error("ValueError: range() arg 3 must not be zero");
    }

    NOINLINE static Value slow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }
    NOINLINE static Value overflow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }

    static ALWAYSINLINE Value invoke_builtin_callback(Value *fp,
                                                      BuiltinFunction *builtin,
                                                      int32_t call_base_reg,
                                                      uint32_t n_args)
    {
        return builtin->callback(&fp[call_base_reg + 1], n_args);
    }

    static ALWAYSINLINE void
    enter_function_frame(Value *&fp, const uint8_t *&pc,
                         CodeObject *&code_object, TValue<Function> fun,
                         int32_t call_base_reg, uint32_t n_args)
    {
        pc += 3;

        Value *new_fp =
            fp + call_base_reg + int32_t(n_args) + FrameHeaderSizeBelowFp + 1;

        initialize_frame_header(new_fp, fp, code_object, pc);

        fp = new_fp;
        code_object = fun.extract()->code_object.extract();
        pc = code_object->code.data();
    }

    NOINLINE static Value op_call_method_without_self(PARAMS)
    {
        int32_t reg = int8_t(pc[1]);
        uint32_t n_user_args = uint8_t(pc[2]);
        Value fun = fp[reg];

        if(unlikely(!fun.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        if(fun.get_ptr()->klass == &BuiltinFunction::klass)
        {
            BuiltinFunction *builtin = fun.get_ptr<BuiltinFunction>();
            if(unlikely(!builtin->accepts_arity(n_user_args)))
            {
                MUSTTAIL return wrong_arity_error(ARGS);
            }

            accumulator =
                invoke_builtin_callback(fp, builtin, reg + 1, n_user_args);

            pc += 3;

            START(0);
            COMPLETE();
        }

        if(unlikely(fun.get_ptr()->klass != &Function::klass))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        enter_function_frame(fp, pc, code_object, TValue<Function>(fun),
                             reg + 1, n_user_args);

        START(0);
        COMPLETE();
    }

    static Value op_lda_constant(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        accumulator = code_object->constant_table[const_offset];
        COMPLETE();
    }

    static Value op_lda_smi(PARAMS)
    {
        START(2);
        int8_t smi = pc[1];
        accumulator = Value::from_smi(smi);
        COMPLETE();
    }

    static Value op_lda_true(PARAMS)
    {
        START(1);
        accumulator = Value::True();
        COMPLETE();
    }

    static Value op_lda_false(PARAMS)
    {
        START(1);
        accumulator = Value::False();
        COMPLETE();
    }

    static Value op_lda_none(PARAMS)
    {
        START(1);
        accumulator = Value::None();
        COMPLETE();
    }

    static Value op_ldar(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        accumulator = fp[reg];
        COMPLETE();
    }

    static Value op_star(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        fp[reg] = accumulator;

        COMPLETE();
    }

#define LDAR_STAR_FASTPATH(idx)                                                \
    static Value op_ldar##idx(PARAMS)                                          \
    {                                                                          \
        START(1);                                                              \
        int8_t reg = idx + cl::FrameHeaderSizeAboveFp;                         \
        accumulator = fp[reg];                                                 \
        COMPLETE();                                                            \
    }                                                                          \
    static Value op_star##idx(PARAMS)                                          \
    {                                                                          \
        START(1);                                                              \
        int8_t reg = idx + cl::FrameHeaderSizeAboveFp;                         \
        fp[reg] = accumulator;                                                 \
        COMPLETE();                                                            \
    }

    LDAR_STAR_FASTPATH(0);
    LDAR_STAR_FASTPATH(1);
    LDAR_STAR_FASTPATH(2);
    LDAR_STAR_FASTPATH(3);
    LDAR_STAR_FASTPATH(4);
    LDAR_STAR_FASTPATH(5);
    LDAR_STAR_FASTPATH(6);
    LDAR_STAR_FASTPATH(7);
    LDAR_STAR_FASTPATH(8);
    LDAR_STAR_FASTPATH(9);
    LDAR_STAR_FASTPATH(10);
    LDAR_STAR_FASTPATH(11);
    LDAR_STAR_FASTPATH(12);
    LDAR_STAR_FASTPATH(13);
    LDAR_STAR_FASTPATH(14);
    LDAR_STAR_FASTPATH(15);
#undef LDAR_STAR_FASTPATH

    NOINLINE static Value op_lda_global_slow_path(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        Value v =
            code_object->module_scope.extract()->get_by_slot_index(slot_idx);
        if(unlikely(v.is_not_present()))
        {
            MUSTTAIL return name_error(ARGS);
        }
        accumulator = v;
        COMPLETE();
    }

    static Value op_lda_global(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        Value v = code_object->module_scope.extract()
                      ->get_by_slot_index_fastpath_only(slot_idx);
        if(unlikely(v.is_not_present()))
        {
            MUSTTAIL return op_lda_global_slow_path(ARGS);
        }
        accumulator = v;
        COMPLETE();
    }

    static Value op_sta_global(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        code_object->module_scope.extract()->set_by_slot_index(slot_idx,
                                                               accumulator);
        COMPLETE();
    }

    static Value op_load_attr(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t const_offset = pc[2];
        TValue<String> attr_name(
            code_object->constant_table[const_offset].as_value());
        accumulator = load_attr(fp[reg], attr_name);
        if(unlikely(accumulator.is_not_present()))
        {
            MUSTTAIL return attribute_error(ARGS);
        }
        COMPLETE();
    }

    static Value op_store_attr(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t const_offset = pc[2];
        TValue<String> attr_name(
            code_object->constant_table[const_offset].as_value());
        if(unlikely(!store_attr(fp[reg], attr_name, accumulator)))
        {
            MUSTTAIL return attribute_assignment_error(ARGS);
        }
        COMPLETE();
    }

    static Value op_load_subscript(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        accumulator = load_subscript(fp[reg], accumulator);
        if(unlikely(accumulator.is_not_present()))
        {
            MUSTTAIL return subscript_error(ARGS);
        }
        COMPLETE();
    }

    static Value op_store_subscript(PARAMS)
    {
        START(3);
        int8_t receiver_reg = pc[1];
        int8_t key_reg = pc[2];
        if(unlikely(
               !store_subscript(fp[receiver_reg], fp[key_reg], accumulator)))
        {
            MUSTTAIL return subscript_error(ARGS);
        }
        COMPLETE();
    }

    static Value op_load_method(PARAMS)
    {
        START(4);
        int8_t receiver_reg = pc[1];
        uint8_t const_offset = pc[2];
        int8_t call_base_reg = pc[3];
        TValue<String> attr_name(
            code_object->constant_table[const_offset].as_value());
        Value callable = Value::not_present();
        Value self = Value::not_present();
        if(unlikely(!load_method(fp[receiver_reg], attr_name, callable, self)))
        {
            MUSTTAIL return method_lookup_error(ARGS);
        }
        fp[call_base_reg] = callable;
        fp[call_base_reg + 1] = self;
        COMPLETE();
    }

    static Value op_add_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_add_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer -= b.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_add(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_add_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer -= a.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_sub_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_sub_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer += b.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_sub(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_sub_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer += a.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_mul(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        Value dest;
        if(unlikely(__builtin_smulll_overflow(a.as.integer, b.get_smi(),
                                              &dest.as.integer)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator = dest;

        COMPLETE();
    }

    static Value op_mul_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        Value dest;
        if(unlikely(__builtin_smulll_overflow(a.as.integer, b.get_smi(),
                                              &dest.as.integer)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator = dest;

        COMPLETE();
    }

    static Value op_left_shift(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0))
            MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        int64_t value = a.get_smi();
        int64_t sign_bits = __builtin_clrsbll(value);
        if(unlikely(uint64_t(shift_count) >= 64 ||
                    (value != 0 &&
                     shift_count + int64_t(value_tag_bits) > sign_bits)))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator.as.integer =
            static_cast<int64_t>(uint64_t(a.as.integer) << shift_count);

        COMPLETE();
    }

    static Value op_left_shift_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        // Bytecode invariant: codegen only emits LeftShiftSmi for shifts 0..63.
        assert(shift_count >= 0 && shift_count < 64);
        int64_t value = a.get_smi();
        int64_t sign_bits = __builtin_clrsbll(value);
        if(unlikely(value != 0 &&
                    shift_count + int64_t(value_tag_bits) > sign_bits))
        {
            MUSTTAIL return overflow_path(ARGS);
        }
        accumulator.as.integer =
            static_cast<int64_t>(uint64_t(a.as.integer) << shift_count);

        COMPLETE();
    }

    static Value op_right_shift(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0))
            MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;

        COMPLETE();
    }

    static Value op_right_shift_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0))
            MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;

        COMPLETE();
    }

    static Value op_test_is(PARAMS)
    {
        START_BINARY_REG_ACC();

        accumulator =
            (a.as.integer == b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_is_not(PARAMS)
    {
        START_BINARY_REG_ACC();
        accumulator =
            (a.as.integer != b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_less(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator =
            (a.as.integer < b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_less_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator =
            (a.as.integer <= b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_greater_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator =
            (a.as.integer >= b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_greater(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator =
            (a.as.integer > b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_REFCOUNTED_PTR()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        // see if we have a bit difference after we clear the bit that promotes
        // booleans to 0/1 integers
        uint64_t difference =
            (a.as.integer ^ b.as.integer) & value_boolean_to_integer_mask;
        accumulator = (difference == 0) ? Value::True() : Value::False();

        COMPLETE();
    }

    static Value op_test_not_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_REFCOUNTED_PTR()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        // see if we have a bit difference after we clear the bit that promotes
        // booleans to 0/1 integers
        uint64_t difference =
            (a.as.integer ^ b.as.integer) & value_boolean_to_integer_mask;
        accumulator = (difference != 0) ? Value::True() : Value::False();

        COMPLETE();
    }

    static Value op_negate(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        if(unlikely(__builtin_sub_overflow(0, a.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer = -accumulator.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static Value op_not(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely((a.as.integer & value_ptr_mask) != 0))
        {
            // this is not an inlined type, go to the slow path
            MUSTTAIL return slow_path(ARGS);
        }
        // however, if this is an inlined type, we can simply test for
        // truthiness using a mask and negate

        if((a.as.integer & value_truthy_mask) != 0)
        {
            accumulator = Value::False();
        }
        else
        {
            accumulator = Value::True();
        }
        COMPLETE();
    }

    static Value op_create_function(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        TValue<CodeObject> code_obj(
            code_object->constant_table[const_offset].as_value());

        accumulator =
            ThreadState::get_active()->make_refcounted_value<Function>(
                code_obj);

        COMPLETE();
    }

    static Value op_create_list(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t n_items = pc[2];

        TValue<List> list =
            ThreadState::get_active()->make_refcounted_value<List>(n_items);
        for(uint8_t idx = 0; idx < n_items; ++idx)
        {
            list.extract()->set_item_unchecked(idx, fp[reg + int32_t(idx)]);
        }
        accumulator = list;

        COMPLETE();
    }

    static Value op_create_dict(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t n_items = pc[2];

        TValue<Dict> dict =
            ThreadState::get_active()->make_refcounted_value<Dict>();
        for(uint8_t idx = 0; idx < n_items; ++idx)
        {
            Value key = fp[reg + int32_t(idx) * 2];
            Value value = fp[reg + int32_t(idx) * 2 + 1];
            dict.extract()->set_item(key, value);
        }
        accumulator = dict;

        COMPLETE();
    }

    static Value op_create_class(PARAMS)
    {
        uint8_t body_const_offset = pc[1];
        TValue<CodeObject> body_code(
            code_object->constant_table[body_const_offset].as_value());

        const uint8_t *return_pc = pc + 2;
        Value *new_fp =
            make_nested_frame(fp, code_object, return_pc, code_object);
        initialize_class_body_frame(new_fp, body_code.extract());

        fp = new_fp;
        code_object = body_code.extract();
        pc = code_object->code.data();

        START(0);
        COMPLETE();
    }

    static Value op_build_class(PARAMS)
    {
        accumulator = build_class_from_frame(fp, code_object,
                                             TValue<String>(code_object->name));

        restore_frame_header(fp, pc, code_object);

        START(0);
        COMPLETE();
    }

    static Value op_jump(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        pc += 3;
        pc += rel_target;

        START(0);
        COMPLETE();
    }

    static Value op_jump_if_true(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        if(unlikely((accumulator.as.integer & value_ptr_mask) != 0))
        {
            // this is not an inlined type, go to the slow path
            MUSTTAIL return slow_path(ARGS);
        }

        pc += 3;
        if(accumulator.is_truthy())
        {
            pc += rel_target;
        }

        START(0);
        COMPLETE();
    }

    static Value op_jump_if_false(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        if(unlikely((accumulator.as.integer & value_ptr_mask) != 0))
        {
            // this is not an inlined type, go to the slow path
            MUSTTAIL return slow_path(ARGS);
        }

        pc += 3;
        if(accumulator.is_falsy())
        {
            pc += rel_target;
        }

        START(0);
        COMPLETE();
    }

    static Value op_call_simple(PARAMS)
    {
        int8_t reg = pc[1];
        uint8_t n_args = pc[2];
        Value fun = fp[reg];

        if(unlikely(!fun.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        if(fun.get_ptr()->klass == &ClassObject::klass)
        {
            if(unlikely(n_args != 0))
            {
                MUSTTAIL return wrong_arity_error(ARGS);
            }

            ClassObject *cls = fun.get_ptr<ClassObject>();
            accumulator = Value::from_oop(
                ThreadState::get_active()->make_refcounted_raw<Instance>(
                    Value::from_oop(cls),
                    Value::from_oop(cls->get_initial_shape())));

            pc += 3;

            START(0);
            COMPLETE();
        }

        if(fun.get_ptr()->klass == &BuiltinFunction::klass)
        {
            BuiltinFunction *builtin = fun.get_ptr<BuiltinFunction>();
            if(unlikely(!builtin->accepts_arity(n_args)))
            {
                MUSTTAIL return wrong_arity_error(ARGS);
            }

            accumulator = invoke_builtin_callback(fp, builtin, reg, n_args);

            pc += 3;

            START(0);
            COMPLETE();
        }

        if(unlikely(fun.get_ptr()->klass != &Function::klass))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        enter_function_frame(fp, pc, code_object, TValue<Function>(fun), reg,
                             n_args);

        START(0);
        COMPLETE();
    }

    static Value op_call_method(PARAMS)
    {
        int32_t reg = int8_t(pc[1]);
        if(unlikely(fp[reg + 1].is_not_present()))
        {
            return op_call_method_without_self(ARGS);
        }

        uint32_t n_user_args = uint8_t(pc[2]);
        Value fun = fp[reg];

        if(unlikely(!fun.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        if(fun.get_ptr()->klass == &BuiltinFunction::klass)
        {
            BuiltinFunction *builtin = fun.get_ptr<BuiltinFunction>();
            uint32_t n_args = n_user_args + 1;
            if(unlikely(!builtin->accepts_arity(n_args)))
            {
                MUSTTAIL return wrong_arity_error(ARGS);
            }

            accumulator = invoke_builtin_callback(fp, builtin, reg, n_args);

            pc += 3;

            START(0);
            COMPLETE();
        }

        if(unlikely(fun.get_ptr()->klass != &Function::klass))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        enter_function_frame(fp, pc, code_object, TValue<Function>(fun), reg,
                             n_user_args + 1);

        START(0);
        COMPLETE();
    }

    static Value op_get_iter(PARAMS)
    {
        START(1);
        if(unlikely(!accumulator.is_ptr()))
        {
            MUSTTAIL return not_iterable_error(ARGS);
        }

        if(unlikely(accumulator.get_ptr()->klass != &RangeIterator::klass))
        {
            MUSTTAIL return not_iterable_error(ARGS);
        }

        COMPLETE();
    }

    static Value op_for_iter(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value iterator_value = fp[reg];

        if(unlikely(!iterator_value.is_ptr() ||
                    iterator_value.get_ptr()->klass != &RangeIterator::klass))
        {
            MUSTTAIL return not_iterator_error(ARGS);
        }

        RangeIterator *iterator = iterator_value.get_ptr<RangeIterator>();
        Value current = iterator->current;
        Value stop = iterator->stop;
        Value step = iterator->step;

        if(unlikely(!current.is_smi() || !stop.is_smi() || !step.is_smi()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        int64_t step_smi = step.get_smi();

        if(unlikely(step_smi == 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        bool exhausted =
            step_smi > 0 ? current_smi >= stop_smi : current_smi <= stop_smi;
        pc += 4;
        if(exhausted)
        {
            pc += rel_target;
        }
        else
        {
            int64_t next_smi = 0;
            if(unlikely(
                   __builtin_add_overflow(current_smi, step_smi, &next_smi)))
            {
                MUSTTAIL return overflow_path(ARGS);
            }
            accumulator = current;
            iterator->current = Value::from_smi(next_smi);
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_prep_range1(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable !=
           ThreadState::get_active()->get_machine()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value stop = fp[reg + 1];
            if(unlikely(!stop.is_integer()))
            {
                MUSTTAIL return range_integer_argument_error(ARGS);
            }
            fp[reg] = Value::from_smi(0);
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_prep_range2(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable !=
           ThreadState::get_active()->get_machine()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value start = fp[reg + 1];
            Value stop = fp[reg + 2];
            if(unlikely(!start.is_integer() || !stop.is_integer()))
            {
                MUSTTAIL return range_integer_argument_error(ARGS);
            }
            fp[reg] = start;
            fp[reg + 1] = stop;
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_prep_range3(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable !=
           ThreadState::get_active()->get_machine()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value start = fp[reg + 1];
            Value stop = fp[reg + 2];
            Value step = fp[reg + 3];
            if(unlikely(!start.is_integer() || !stop.is_integer() ||
                        !step.is_integer()))
            {
                MUSTTAIL return range_integer_argument_error(ARGS);
            }
            if(unlikely(step.get_smi() == 0))
            {
                MUSTTAIL return range_zero_step_error(ARGS);
            }
            fp[reg] = start;
            fp[reg + 1] = stop;
            fp[reg + 2] = step;
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_iter_range1(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value current = fp[reg];
        Value stop = fp[reg + 1];

        if(unlikely(!current.is_smi() || !stop.is_smi()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        pc += 4;
        if(current_smi >= stop_smi)
        {
            pc += rel_target;
        }
        else
        {
            int64_t next_smi = 0;
            if(unlikely(__builtin_add_overflow(current_smi, 1, &next_smi)))
            {
                MUSTTAIL return overflow_path(ARGS);
            }
            accumulator = current;
            fp[reg] = Value::from_smi(next_smi);
        }

        START(0);
        COMPLETE();
    }

    static Value op_for_iter_range_step(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value current = fp[reg];
        Value stop = fp[reg + 1];
        Value step = fp[reg + 2];

        if(unlikely(!current.is_smi() || !stop.is_smi() || !step.is_smi()))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        int64_t step_smi = step.get_smi();
        if(unlikely(step_smi == 0))
        {
            MUSTTAIL return slow_path(ARGS);
        }

        bool exhausted =
            step_smi > 0 ? current_smi >= stop_smi : current_smi <= stop_smi;
        pc += 4;
        if(exhausted)
        {
            pc += rel_target;
        }
        else
        {
            int64_t next_smi = 0;
            if(unlikely(
                   __builtin_add_overflow(current_smi, step_smi, &next_smi)))
            {
                MUSTTAIL return overflow_path(ARGS);
            }
            accumulator = current;
            fp[reg] = Value::from_smi(next_smi);
        }

        START(0);
        COMPLETE();
    }

    static Value op_return(PARAMS)
    {
        restore_frame_header(fp, pc, code_object);

        START(0);
        COMPLETE();
    }

    static Value op_halt(PARAMS)
    {
        START(1);
        return accumulator;
        COMPLETE();
    }

    DispatchTable make_dispatch_table()
    {
        DispatchTable tbl;
        for(size_t i = 0; i < BytecodeTableSize; ++i)
        {
            tbl.table[i] = raise_unknown_opcode_exception;
        }
#define SET_TABLE_ENTRY(bytecode, fun) tbl.table[size_t(bytecode)] = fun
        SET_TABLE_ENTRY(Bytecode::Ldar, op_ldar);
        SET_TABLE_ENTRY(Bytecode::Star, op_star);
        SET_TABLE_ENTRY(Bytecode::LdaConstant, op_lda_constant);
        SET_TABLE_ENTRY(Bytecode::LdaSmi, op_lda_smi);
        SET_TABLE_ENTRY(Bytecode::LdaTrue, op_lda_true);
        SET_TABLE_ENTRY(Bytecode::LdaFalse, op_lda_false);
        SET_TABLE_ENTRY(Bytecode::LdaNone, op_lda_none);
        SET_TABLE_ENTRY(Bytecode::Add, op_add);
        SET_TABLE_ENTRY(Bytecode::AddSmi, op_add_smi);
        SET_TABLE_ENTRY(Bytecode::Sub, op_sub);
        SET_TABLE_ENTRY(Bytecode::SubSmi, op_sub_smi);
        SET_TABLE_ENTRY(Bytecode::Mul, op_mul);
        SET_TABLE_ENTRY(Bytecode::MulSmi, op_mul_smi);

        SET_TABLE_ENTRY(Bytecode::LeftShift, op_left_shift);
        SET_TABLE_ENTRY(Bytecode::LeftShiftSmi, op_left_shift_smi);
        SET_TABLE_ENTRY(Bytecode::RightShift, op_right_shift);
        SET_TABLE_ENTRY(Bytecode::RightShiftSmi, op_right_shift_smi);

        SET_TABLE_ENTRY(Bytecode::TestIs, op_test_is);
        SET_TABLE_ENTRY(Bytecode::TestIsNot, op_test_is_not);
        SET_TABLE_ENTRY(Bytecode::TestEqual, op_test_equal);
        SET_TABLE_ENTRY(Bytecode::TestNotEqual, op_test_not_equal);
        SET_TABLE_ENTRY(Bytecode::TestLess, op_test_less);
        SET_TABLE_ENTRY(Bytecode::TestLessEqual, op_test_less_equal);
        SET_TABLE_ENTRY(Bytecode::TestGreaterEqual, op_test_greater_equal);
        SET_TABLE_ENTRY(Bytecode::TestGreater, op_test_greater);

        SET_TABLE_ENTRY(Bytecode::LdaGlobal, op_lda_global);
        SET_TABLE_ENTRY(Bytecode::StaGlobal, op_sta_global);
        SET_TABLE_ENTRY(Bytecode::LoadAttr, op_load_attr);
        SET_TABLE_ENTRY(Bytecode::StoreAttr, op_store_attr);
        SET_TABLE_ENTRY(Bytecode::LoadSubscript, op_load_subscript);
        SET_TABLE_ENTRY(Bytecode::StoreSubscript, op_store_subscript);
        SET_TABLE_ENTRY(Bytecode::LoadMethod, op_load_method);

        SET_TABLE_ENTRY(Bytecode::Negate, op_negate);
        SET_TABLE_ENTRY(Bytecode::Not, op_not);

        SET_TABLE_ENTRY(Bytecode::CreateDict, op_create_dict);
        SET_TABLE_ENTRY(Bytecode::CreateList, op_create_list);
        SET_TABLE_ENTRY(Bytecode::CreateFunction, op_create_function);
        SET_TABLE_ENTRY(Bytecode::CreateClass, op_create_class);
        SET_TABLE_ENTRY(Bytecode::BuildClass, op_build_class);

        SET_TABLE_ENTRY(Bytecode::CallSimple, op_call_simple);
        SET_TABLE_ENTRY(Bytecode::CallMethod, op_call_method);
        SET_TABLE_ENTRY(Bytecode::GetIter, op_get_iter);
        SET_TABLE_ENTRY(Bytecode::ForIter, op_for_iter);
        SET_TABLE_ENTRY(Bytecode::ForPrepRange1, op_for_prep_range1);
        SET_TABLE_ENTRY(Bytecode::ForPrepRange2, op_for_prep_range2);
        SET_TABLE_ENTRY(Bytecode::ForPrepRange3, op_for_prep_range3);
        SET_TABLE_ENTRY(Bytecode::ForIterRange1, op_for_iter_range1);
        SET_TABLE_ENTRY(Bytecode::ForIterRangeStep, op_for_iter_range_step);

        SET_TABLE_ENTRY(Bytecode::Jump, op_jump);
        SET_TABLE_ENTRY(Bytecode::JumpIfTrue, op_jump_if_true);
        SET_TABLE_ENTRY(Bytecode::JumpIfFalse, op_jump_if_false);
        SET_TABLE_ENTRY(Bytecode::Return, op_return);
        SET_TABLE_ENTRY(Bytecode::Halt, op_halt);

#define REGISTER_LDAR_STAR_FASTPATH(idx)                                       \
    SET_TABLE_ENTRY(Bytecode::Ldar##idx, op_ldar##idx);                        \
    SET_TABLE_ENTRY(Bytecode::Star##idx, op_star##idx);

        REGISTER_LDAR_STAR_FASTPATH(0);
        REGISTER_LDAR_STAR_FASTPATH(1);
        REGISTER_LDAR_STAR_FASTPATH(2);
        REGISTER_LDAR_STAR_FASTPATH(3);
        REGISTER_LDAR_STAR_FASTPATH(4);
        REGISTER_LDAR_STAR_FASTPATH(5);
        REGISTER_LDAR_STAR_FASTPATH(6);
        REGISTER_LDAR_STAR_FASTPATH(7);
        REGISTER_LDAR_STAR_FASTPATH(8);
        REGISTER_LDAR_STAR_FASTPATH(9);
        REGISTER_LDAR_STAR_FASTPATH(10);
        REGISTER_LDAR_STAR_FASTPATH(11);
        REGISTER_LDAR_STAR_FASTPATH(12);
        REGISTER_LDAR_STAR_FASTPATH(13);
        REGISTER_LDAR_STAR_FASTPATH(14);
        REGISTER_LDAR_STAR_FASTPATH(15);
#undef REGISTER_LDAR_STAR_FASTPATH

        return tbl;
    }

    DispatchTable dispatch_table = make_dispatch_table();

    Value run_interpreter(Value *fp, CodeObject *code_object, uint32_t start_pc)
    {
        const uint8_t *pc = &code_object->code[start_pc];
        void *dispatch = reinterpret_cast<void *>(&dispatch_table);
        Value accumulator = Value::from_smi(0);  // init accumulator to 0

        // do the initial dispatch
        auto *dispatch_fun = dispatch_table.table[*pc];
        return dispatch_fun(ARGS);
    }

}  // namespace cl
