#include "interpreter.h"

#include "value.h"
#include "code_object.h"
#include "function.h"
#include "thread_state.h"
#include <fmt/core.h>
#include "code_object_print.h"

namespace cl
{


#define PARAMS Value *fp, const uint8_t *pc, Value accumulator, void *dispatch, CodeObject *code_object
#define ARGS fp, pc, accumulator, dispatch, code_object

    using DispatchTableEntry = Value (*)(PARAMS);

    struct DispatchTable
    {
        DispatchTableEntry table[BytecodeTableSize];
    };



#define START(len) static constexpr uint32_t instr_len = len; auto *dispatch_fun = reinterpret_cast<DispatchTable *>(dispatch)->table[pc[instr_len]]
#define COMPLETE() pc += instr_len; MUSTTAIL return dispatch_fun(ARGS)

#define START_BINARY_REG_ACC()               \
    START(2);                                \
    int8_t reg = pc[1];                     \
    Value a = fp[reg];       \
    Value b = accumulator

#define START_BINARY_ACC_SMI()               \
    START(2);                                \
    Value a = accumulator;                 \
    Value b = Value::from_smi(int8_t(pc[1]))

#define START_UNARY_ACC()                    \
    START(1);                                \
    Value a = accumulator




#define A_NOT_SMI() ((a.as.integer & value_not_smi_mask) != 0)
#define A_OR_B_NOT_SMI() (((a.as.integer | b.as.integer) & value_not_smi_mask) != 0)
#define A_OR_B_REFCOUNTED_PTR() (((a.as.integer | b.as.integer) & value_refcounted_ptr_tag) != 0)


    static uint32_t read_uint32_le(const uint8_t *p)
    {
        return
            (p[0] <<  0) |
            (p[1] <<  8) |
            (p[1] << 16) |
            (p[1] << 24);
    }

    static int16_t read_int16_le(const uint8_t *p)
    {
#if 1
        const int16_t *ptr = reinterpret_cast<const int16_t *>(p);
        return *ptr;
#else
        return
            (p[0] <<  0) |
            (p[1] <<  8);
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

    NOINLINE Value name_error(PARAMS)
    {
        throw std::runtime_error("NameError");
    }

    NOINLINE Value not_callable_error(PARAMS)
    {
        throw std::runtime_error("TypeError: object is not callable");
    }

    NOINLINE static Value slow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
    }
    NOINLINE static Value overflow_path(PARAMS)
    {
        MUSTTAIL return raise_generic_exception(ARGS);
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

    NOINLINE static Value op_lda_global_slow_path(PARAMS)
    {
        START(5);
        int32_t slot_idx = read_uint32_le(&pc[1]);
        Value v = code_object->module_scope.get_ptr<Scope>()->get_by_slot_index(slot_idx);
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
        Value v = code_object->module_scope.get_ptr<Scope>()->get_by_slot_index_fastpath_only(slot_idx);
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
        code_object->module_scope.get_ptr<Scope>()->set_by_slot_index(slot_idx, accumulator);
        COMPLETE();
    }


    static Value op_add_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        if(unlikely(__builtin_saddll_overflow(a.as.integer, b.as.integer, &accumulator.as.integer)))
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
        if(unlikely(__builtin_saddll_overflow(a.as.integer, b.as.integer, &accumulator.as.integer)))
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
        if(unlikely(__builtin_ssubll_overflow(a.as.integer, b.as.integer, &accumulator.as.integer)))
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
        if(unlikely(__builtin_ssubll_overflow(a.as.integer, b.as.integer, &accumulator.as.integer)))
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
        if(unlikely(__builtin_smulll_overflow(a.as.integer, b.get_smi(), &dest.as.integer)))
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
        if(unlikely(__builtin_smulll_overflow(a.as.integer, b.get_smi(), &dest.as.integer)))
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
        if(unlikely(shift_count < 0)) MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer << shift_count;
        /* TODO need to test overflow here */

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
        if(unlikely(shift_count < 0)) MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer << shift_count;
        /* TODO need to test overflow here */

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
        if(unlikely(shift_count < 0)) MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
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
        if(unlikely(shift_count < 0)) MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;

        COMPLETE();
    }



    static Value op_test_is(PARAMS)
    {
        START_BINARY_REG_ACC();

        accumulator = (a.as.integer == b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }


    static Value op_test_is_not(PARAMS)
    {
        START_BINARY_REG_ACC();
        accumulator = (a.as.integer != b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_less(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator = (a.as.integer < b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_less_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator = (a.as.integer <= b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_greater_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator = (a.as.integer >= b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_greater(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        accumulator = (a.as.integer > b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static Value op_test_equal(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_REFCOUNTED_PTR()))
        {
            MUSTTAIL return slow_path(ARGS);
        }
        // see if we have a bit difference after we clear the bit that promotes booleans to 0/1 integers
        uint64_t difference = (a.as.integer ^ b.as.integer) & value_boolean_to_integer_mask;
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
        // see if we have a bit difference after we clear the bit that promotes booleans to 0/1 integers
        uint64_t difference = (a.as.integer ^ b.as.integer) & value_boolean_to_integer_mask;
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

        if(unlikely(__builtin_ssubll_overflow(0, a.as.integer, &accumulator.as.integer)))
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
        // however, if this is an inlined type, we can simply test for truthiness using a mask and negate

        if((a.as.integer & value_truthy_mask) != 0)
        {
            accumulator = Value::False();
        } else {
            accumulator = Value::True();
        }
        COMPLETE();


    }

    static Value op_create_function(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        Value code_obj = code_object->constant_table[const_offset];

        accumulator = Value::from_oop(new(ThreadState::get_active()->allocate_refcounted(sizeof(Function)))Function(code_obj));

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

        if(unlikely(!fun.is_ptr() || fun.get_ptr()->klass != &Function::klass))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        pc += 3;

        // must save off pc, old code object and fp
        Value *new_fp = fp + reg - n_args - FrameHeaderSizeAboveFp;

        // these aren't really values. we're just going to whack them in and ask the refcounter to ignore them.
        new_fp[0].as.ptr = (Object *)fp;
        new_fp[-1] = Value::from_oop(code_object);
        new_fp[-2].as.ptr = (Object *)pc;

        fp = new_fp;
        code_object = fun.get_ptr<Function>()->code_object.get_ptr<CodeObject>();
        pc = code_object->code.data();

        START(0);
        COMPLETE();
    }


    static Value op_return(PARAMS)
    {
        pc = (const uint8_t *)fp[-2].as.ptr;
        code_object = fp[-1].get_ptr<CodeObject>();
        fp = (Value *)fp[0].as.ptr;

        START(0);
        COMPLETE();
    }

    static Value op_halt(PARAMS)
    {
        START(1);
        return accumulator;
        COMPLETE();
    }

    constexpr DispatchTable make_dispatch_table()
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

        SET_TABLE_ENTRY(Bytecode::Negate, op_negate);
        SET_TABLE_ENTRY(Bytecode::Not, op_not);


        SET_TABLE_ENTRY(Bytecode::CreateFunction, op_create_function);

        SET_TABLE_ENTRY(Bytecode::CallSimple, op_call_simple);

        SET_TABLE_ENTRY(Bytecode::Jump, op_jump);
        SET_TABLE_ENTRY(Bytecode::JumpIfTrue, op_jump_if_true);
        SET_TABLE_ENTRY(Bytecode::JumpIfFalse, op_jump_if_false);
        SET_TABLE_ENTRY(Bytecode::Return, op_return);
        SET_TABLE_ENTRY(Bytecode::Halt, op_halt);
        return tbl;
    }


    DispatchTable dispatch_table = make_dispatch_table();


    Value run_interpreter(Value *fp, CodeObject *code_object, uint32_t start_pc)
    {
        const uint8_t *pc = &code_object->code[start_pc];
        void *dispatch = reinterpret_cast<void *>(&dispatch_table);
        Value accumulator = Value::from_smi(0); // init accumulator to 0





        // do the initial dispatch
        auto *dispatch_fun = dispatch_table.table[*pc];
        return dispatch_fun(ARGS);


    }


}
