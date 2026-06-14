#include "runtime/interpreter.h"

#include "builtin_types/dict.h"
#include "builtin_types/float.h"
#include "builtin_types/list.h"
#include "builtin_types/range_iterator.h"
#include "builtin_types/slice.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object.h"
#include "bytecode/code_object_print.h"
#include "import_system/import_system.h"
#include "import_system/module_global.h"
#include "native/native_module_api_internal.h"
#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/instance.h"
#include "object_model/refcount.h"
#include "object_model/slot_dict.h"
#include "object_model/validity_cell.h"
#include "object_model/value.h"
#include "runtime/exception_handling.h"
#include "runtime/exception_object.h"
#include "runtime/method_call.h"
#include "runtime/operator_frame.h"
#include "runtime/operator_walk.h"
#include "runtime/runtime_helpers.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fmt/core.h>
#include <fmt/format.h>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cl
{

#define PARAMS                                                                 \
    Value accumulator, Value *fp, const uint8_t *pc, void *dispatch,           \
        CodeObject *code_object, ThreadState *thread
#define ARGS accumulator, fp, pc, dispatch, code_object, thread
#if (defined(__clang__) && __has_attribute(preserve_none)) ||                  \
    (defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 16)
#define INTERP_CC __attribute__((preserve_none))
#else
#define INTERP_CC
#endif

    using DispatchTableEntry = Value(INTERP_CC *)(PARAMS);

    static ALWAYSINLINE DispatchTableEntry
    dispatch_binary_operator_from_continuation(
        ThreadState *thread, Value *&fp, const uint8_t *&pc, void *dispatch,
        CodeObject *&code_object, Value &accumulator,
        const uint8_t *continuation_pc, const uint8_t *next_pc);

    [[maybe_unused]] static ALWAYSINLINE bool is_stack_frame_aligned(Value *fp)
    {
        return (reinterpret_cast<uintptr_t>(fp) % FrameAlignmentBytes) == 0;
    }

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

    static int16_t read_int16_le(const uint8_t *p)
    {
#if 1
        const int16_t *ptr = reinterpret_cast<const int16_t *>(p);
        return *ptr;
#else
        return (p[0] << 0) | (p[1] << 8);
#endif
    }

    [[maybe_unused]] static NOINLINE ExceptionalTarget
    resolve_exceptional_frame_exit(ThreadState *thread, Value *fp,
                                   const uint8_t *pc, CodeObject *code_object);

    static std::wstring format_name_error_message(TValue<String> name)
    {
        String *str = name.extract();
        size_t n_chars = size_t(str->count.extract());
        std::wstring result = L"name '";
        result.append(str->data, n_chars);
        result += L"' is not defined";
        return result;
    }

    NOINLINE INTERP_CC Value raise_unknown_opcode_exception(PARAMS)
    {
        (void)thread->set_pending_builtin_exception_string(L"SystemError",
                                                           L"Unknown opcode");
        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    static NOINLINE ExceptionalTarget
    set_builtin_exception_and_resolve_frame_exit(ThreadState *thread, Value *fp,
                                                 const uint8_t *pc,
                                                 CodeObject *code_object,
                                                 const wchar_t *type_name,
                                                 const wchar_t *message)
    {
        (void)thread->set_pending_builtin_exception_string(type_name, message);
        return resolve_exceptional_frame_exit(thread, fp, pc, code_object);
    }

    static NOINLINE ExceptionalTarget set_name_error_and_resolve_frame_exit(
        ThreadState *thread, Value *fp, const uint8_t *pc,
        CodeObject *code_object, TValue<String> name)
    {
        std::wstring message = format_name_error_message(name);
        (void)thread->set_pending_builtin_exception_string(L"NameError",
                                                           message.c_str());
        return resolve_exceptional_frame_exit(thread, fp, pc, code_object);
    }

    NOINLINE INTERP_CC Value raise_value_error_negative_shift_count(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"ValueError",
            L"negative shift count");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value raise_overflow_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"OverflowError", L"integer overflow");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value zero_division_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"ZeroDivisionError",
            L"division by zero");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value unsupported_division_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"unsupported operand type(s) for /");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value unsupported_int_division_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"unsupported operand type(s) for //");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value unsupported_modulo_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"unsupported operand type(s) for %");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value unsupported_subtraction_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"unsupported operand type(s) for -");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value unsupported_multiplication_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"unsupported operand type(s) for *");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value unsupported_unary_arithmetic_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"unsupported operand type for unary arithmetic");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value unsupported_shift_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"unsupported operand type(s) for shift");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value unsupported_truthiness_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"unsupported truthiness for object");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value invalid_range_iteration_state_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"SystemError",
            L"invalid range iteration state");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value active_exception_required_system_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"SystemError",
            L"active exception required");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value exception_handler_type_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"catching classes that do not inherit from BaseException is not "
            L"implemented yet");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value
    exception_marker_without_pending_exception_system_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"SystemError",
            L"exception marker without pending exception");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value
    exception_marker_native_return_without_pending_exception_system_error(
        PARAMS)
    {
        START(1);
        (void)thread->set_pending_builtin_exception_string(
            L"SystemError",
            L"exception marker native return without pending exception");
        Value *restored_fp = (Value *)fp[FrameHeaderPreviousFpOffset].as.ptr;
        thread->set_clover_frame_frontier(restored_fp);
        return Value::exception_marker();
        COMPLETE();
    }

    NOINLINE INTERP_CC Value local_name_error(PARAMS)
    {
        int8_t reg = pc[1];
        uint32_t slot_idx = code_object->decode_reg(reg);
        assert(slot_idx < code_object->local_scope.extract()->size());
        ExceptionalTarget target = set_name_error_and_resolve_frame_exit(
            thread, fp, pc, code_object,
            code_object->local_scope.extract()->get_name_by_slot_index(
                slot_idx));
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value module_global_name_error(PARAMS)
    {
        uint8_t name_idx = pc[1];
        TValue<String> name = TValue<String>::from_value_assumed(
            code_object->constant_table[name_idx].value());
        ExceptionalTarget target = set_name_error_and_resolve_frame_exit(
            thread, fp, pc, code_object, name);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value module_global_assignment_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"NameError", L"cannot assign global");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value not_callable_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"object is not callable");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value attribute_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"AttributeError", L"");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value attribute_assignment_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"AttributeError",
            L"cannot assign attribute");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value subscript_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"object is not subscriptable");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value subscript_assignment_error(PARAMS)
    {
        int8_t first_arg_reg = pc[1];
        Value receiver = fp[first_arg_reg];
        const wchar_t *message =
            receiver.is_ptr() && receiver.get_ptr()->native_layout_id() ==
                                     NativeLayoutId::Tuple
                ? L"'tuple' object does not support item assignment"
                : L"object is not subscriptable";
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError", message);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value subscript_deletion_error(PARAMS)
    {
        int8_t first_arg_reg = pc[1];
        Value receiver = fp[first_arg_reg];
        const wchar_t *message =
            receiver.is_ptr() && receiver.get_ptr()->native_layout_id() ==
                                     NativeLayoutId::Tuple
                ? L"'tuple' object does not support item deletion"
                : L"object is not subscriptable";
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError", message);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value propagate_pending_exception(PARAMS)
    {
        assert(thread->has_pending_exception());
        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

// Unwrap an Expected-like result inside an opcode handler. Unlike CL_TRY, this
// dispatches through the interpreter exception table instead of returning the
// exception marker directly.
#define INTERP_TRY(expr)                                                       \
    ({                                                                         \
        auto cl_opcode_try_result = (expr);                                    \
        if(unlikely(!cl_opcode_try_result))                                    \
        {                                                                      \
            MUSTTAIL return propagate_pending_exception(ARGS);                 \
        }                                                                      \
        cl_opcode_try_result.value();                                          \
    })

    NOINLINE INTERP_CC Value method_lookup_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"AttributeError", L"");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value descriptor_dispatch_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"descriptor __get__ requires interpreter dispatch");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value wrong_arity_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"wrong number of arguments");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value keyword_call_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"invalid keyword argument");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    static constexpr size_t kObjectCacheLineBytes = 128;
    static constexpr size_t kObjectCacheLineStartOffset = 16;
    static constexpr uint32_t kMaxFactoryInlineSlotCount =
        (kObjectCacheLineBytes - kObjectCacheLineStartOffset -
         sizeof(Instance)) /
        sizeof(Value);
    static constexpr uint32_t kDefaultFactoryInlineSlotCount =
        kMaxFactoryInlineSlotCount - 1;
    static_assert(sizeof(Instance) +
                      sizeof(Value) * kDefaultFactoryInlineSlotCount <=
                  kObjectCacheLineBytes - kObjectCacheLineStartOffset);

    static ALWAYSINLINE void
    initialize_frame_header(Value *new_fp, Value *previous_fp,
                            CodeObject *return_code_object,
                            const uint8_t *return_pc)
    {
        // these aren't really values. we're just going to whack them in and
        // ask the refcounter to ignore them.
        new_fp[FrameHeaderPreviousFpOffset].as.ptr = (Object *)previous_fp;
        new_fp[FrameHeaderReturnCodeObjectOffset] =
            Value::from_oop(return_code_object);
        new_fp[FrameHeaderReturnPcOffset].as.ptr = (Object *)return_pc;
    }

    static ALWAYSINLINE void restore_frame_header(Value *&fp,
                                                  const uint8_t *&pc,
                                                  CodeObject *&code_object)
    {
        pc = (const uint8_t *)fp[FrameHeaderReturnPcOffset].as.ptr;
        code_object =
            fp[FrameHeaderReturnCodeObjectOffset].get_ptr<CodeObject>();
        fp = (Value *)fp[FrameHeaderPreviousFpOffset].as.ptr;
    }

    static ALWAYSINLINE Value *
    lowest_live_stack_slot_for_current_frame(Value *fp, CodeObject *code_object)
    {
        uint32_t n_below_frame_slots =
            code_object->get_padded_n_ordinary_below_frame_slots();
        return fp - int32_t(n_below_frame_slots);
    }

    NOINLINE static INTERP_CC Value op_committed_safepoint_slow(PARAMS)
    {
        thread->publish_safepoint_scan_record(
            lowest_live_stack_slot_for_current_frame(fp, code_object),
            Value::not_present());
        thread->handle_safepoint(accumulator, fp, pc, code_object);

        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value
    op_committed_safepoint_with_accumulator_slow(PARAMS)
    {
        thread->publish_safepoint_scan_record(
            lowest_live_stack_slot_for_current_frame(fp, code_object),
            accumulator);
        thread->handle_safepoint(accumulator, fp, pc, code_object);

        START(0);
        COMPLETE();
    }

    [[maybe_unused]] static NOINLINE ExceptionalTarget
    resolve_exceptional_frame_exit(ThreadState *thread, Value *fp,
                                   const uint8_t *pc, CodeObject *code_object)
    {
        assert(thread->has_pending_exception());

        const uint8_t *continuation_pc = pc + 1;
        for(;;)
        {
            // Exception tables cover the bytecode instruction that raised or
            // returned into an exceptional path. The loop carries continuation
            // pcs: the first iteration uses the byte after the raising
            // instruction, and caller-frame iterations use the saved return pc.
            // Subtracting one byte is sufficient because no instruction is
            // smaller than one byte. For multi-byte instructions this may land
            // in the middle of the instruction, but the protected table range
            // covers the whole instruction, so membership is unchanged.
            uint32_t continuation_offset =
                code_object->offset_for_interpreted_pc(continuation_pc);
            assert(continuation_offset > 0);
            const ExceptionTableEntry *entry =
                code_object->find_exception_handler(continuation_offset - 1);
            if(entry != nullptr)
            {
                return {
                    fp, code_object,
                    code_object->interpreted_pc_for_offset(entry->handler_pc),
                    nullptr};
            }

            restore_frame_header(fp, continuation_pc, code_object);
        }
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
            fp[body_code->encode_reg(slot_idx)] = Value::not_present();
        }
    }

    static Expected<void>
    reject_set_name_notifications_until_supported(Value *fp,
                                                  CodeObject *body_code)
    {
        TValue<String> set_name_name(interned_string(L"__set_name__"));

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

            Value callable;
            Value self;
            if(load_method(value, set_name_name, callable, self))
            {
                return Expected<void>::raise_exception(
                    L"TypeError",
                    L"__set_name__ notifications are not implemented yet");
            }
        }
        return Expected<void>::ok();
    }

    static constexpr uint32_t ClassBodyNameParameter = 0;
    static constexpr uint32_t ClassBodyBasesParameter = 1;

    static Expected<Value> build_class_from_frame(ThreadState *thread,
                                                  Value *fp,
                                                  CodeObject *body_code)
    {
        TValue<String> class_name = TValue<String>::from_value_assumed(
            fp[body_code->encode_reg(ClassBodyNameParameter)]);
        TValue<Tuple> bases = TValue<Tuple>::from_value_assumed(
            fp[body_code->encode_reg(ClassBodyBasesParameter)]);

        TValue<ClassObject> cls = CL_TRY(ClassObject::make(
            thread, thread->get_machine()->type_class(), class_name,
            kDefaultFactoryInlineSlotCount, bases, NativeLayoutId::Instance));
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
            if(!cls.extract()->set_own_property(
                   local_scope->get_name_by_slot_index(slot_idx), value))
            {
                return Expected<Value>::raise_exception(
                    L"TypeError", L"cannot set read-only class attribute");
            }
        }
        CL_TRY(reject_set_name_notifications_until_supported(fp, body_code));
        return Expected<Value>::ok(Value::from_oop(cls.extract()));
    }

    NOINLINE INTERP_CC Value not_iterator_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"object is not an iterator");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value range_integer_argument_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"range() arguments must be integers");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value range_zero_step_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"ValueError",
            L"range() arg 3 must not be zero");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value init_returned_non_none_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"__init__ should return None, not a value");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value assertion_error(PARAMS)
    {
        (void)thread->set_pending_builtin_exception_none(L"AssertionError");
        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value assertion_error_with_message(PARAMS)
    {
        (void)thread->set_pending_exception_object(make_exception_object(
            thread,
            TValue<ClassObject>::from_oop(
                thread->class_for_builtin_name(L"AssertionError")),
            INTERP_TRY(TValue<String>::from_value_checked(accumulator))));
        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE static void materialize_pending_stop_iteration(ThreadState *thread)
    {
        Value value = thread->pending_stop_iteration_value();
        ClassObject *stop_iteration =
            thread->class_for_native_layout(NativeLayoutId::StopIteration);
        TValue<StopIterationObject> exception = make_stop_iteration_object(
            thread, TValue<ClassObject>::from_oop(stop_iteration), value);
        (void)thread->set_pending_exception_object(
            TValue<Exception>::from_value_unchecked(exception.raw_value()));
    }

    static INTERP_CC Value op_lda_active_exception(PARAMS)
    {
        if(!thread->has_pending_exception())
        {
            MUSTTAIL return active_exception_required_system_error(ARGS);
        }
        if(thread->pending_exception_kind() ==
           PendingExceptionKind::StopIteration)
        {
            materialize_pending_stop_iteration(thread);
        }
        else if(thread->pending_exception_kind() !=
                PendingExceptionKind::Object)
        {
            MUSTTAIL return active_exception_required_system_error(ARGS);
        }

        accumulator = thread->pending_exception_object().raw_value();
        START(1);
        COMPLETE();
    }

    static INTERP_CC Value op_active_exception_is_instance(PARAMS)
    {
        if(!thread->has_pending_exception())
        {
            MUSTTAIL return active_exception_required_system_error(ARGS);
        }
        if(!can_convert_to<ClassObject>(accumulator))
        {
            MUSTTAIL return exception_handler_type_error(ARGS);
        }

        ClassObject *handler_class = accumulator.get_ptr<ClassObject>();
        ClassObject *exception_class = nullptr;
        switch(thread->pending_exception_kind())
        {
            case PendingExceptionKind::Object:
                exception_class = thread->pending_exception_object()
                                      .extract()
                                      ->get_shape()
                                      ->get_class();
                break;
            case PendingExceptionKind::StopIteration:
                exception_class = thread->class_for_native_layout(
                    NativeLayoutId::StopIteration);
                break;
            case PendingExceptionKind::None:
                MUSTTAIL return active_exception_required_system_error(ARGS);
        }

        accumulator = exception_class == handler_class ||
                              is_subclass_of(exception_class, handler_class)
                          ? Value::True()
                          : Value::False();
        START(1);
        COMPLETE();
    }

    static INTERP_CC Value op_drain_active_exception_into(PARAMS)
    {
        if(!thread->has_pending_exception())
        {
            MUSTTAIL return active_exception_required_system_error(ARGS);
        }
        if(thread->pending_exception_kind() ==
           PendingExceptionKind::StopIteration)
        {
            materialize_pending_stop_iteration(thread);
        }
        else if(thread->pending_exception_kind() !=
                PendingExceptionKind::Object)
        {
            MUSTTAIL return active_exception_required_system_error(ARGS);
        }

        START(2);
        int8_t reg = pc[1];
        fp[reg] = thread->pending_exception_object().raw_value();
        thread->clear_pending_exception();
        COMPLETE();
    }

    static INTERP_CC Value op_clear_active_exception(PARAMS)
    {
        thread->clear_pending_exception();
        START(1);
        COMPLETE();
    }

    static INTERP_CC Value op_reraise_active_exception(PARAMS)
    {
        if(!thread->has_pending_exception())
        {
            MUSTTAIL return active_exception_required_system_error(ARGS);
        }

        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE static TValue<Exception>
    make_raise_exception_object(ThreadState *thread, Value raised)
    {
        if(TValueTraits<Exception>::is_instance(raised))
        {
            return TValue<Exception>::from_value_assumed(raised);
        }
        if(can_convert_to<ClassObject>(raised))
        {
            ClassObject *cls = raised.get_ptr<ClassObject>();
            if(!is_subclass_of(cls, thread->class_for_native_layout(
                                        NativeLayoutId::Exception)))
            {
                return make_exception_object(
                    thread,
                    TValue<ClassObject>::from_oop(
                        thread->class_for_builtin_name(L"TypeError")),
                    L"exceptions must derive from BaseException");
            }
            if(cls ==
               thread->class_for_native_layout(NativeLayoutId::StopIteration))
            {
                return TValue<Exception>::from_value_unchecked(
                    make_stop_iteration_object(
                        thread, TValue<ClassObject>::from_oop(cls))
                        .raw_value());
            }

            return make_exception_object(
                thread, TValue<ClassObject>::from_oop(cls), L"");
        }

        return make_exception_object(
            thread,
            TValue<ClassObject>::from_oop(
                thread->class_for_builtin_name(L"TypeError")),
            L"exceptions must derive from BaseException");
    }

    NOINLINE static void set_exception_context(ThreadState *thread,
                                               TValue<Exception> raised,
                                               Value context)
    {
        [[maybe_unused]] bool ok = raised.extract()->set_own_property(
            thread->get_machine()->get_or_create_interned_string_value(
                L"__context__"),
            context);
        assert(ok);
    }

    NOINLINE INTERP_CC Value raise_unwind(PARAMS)
    {
        (void)thread->set_pending_exception_object(
            make_raise_exception_object(thread, accumulator));

        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value raise_unwind_with_context(PARAMS)
    {
        int8_t context_reg = pc[1];
        TValue<Exception> raised =
            make_raise_exception_object(thread, accumulator);
        set_exception_context(thread, raised, fp[context_reg]);
        (void)thread->set_pending_exception_object(raised);

        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE INTERP_CC Value raise_bare(PARAMS)
    {
        (void)thread->set_pending_builtin_exception_string(
            L"RuntimeError", L"No active exception to reraise");

        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value overflow_path(PARAMS)
    {
        MUSTTAIL return raise_overflow_error(ARGS);
    }

    NOINLINE static INTERP_CC Value op_not_float_truthiness(PARAMS)
    {
        START(1);
        if(!can_convert_to<Float>(accumulator))
        {
            MUSTTAIL return unsupported_truthiness_error(ARGS);
        }

        accumulator = accumulator.get_ptr<Float>()->value != 0.0
                          ? Value::False()
                          : Value::True();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_jump_if_true_float_truthiness(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        if(!can_convert_to<Float>(accumulator))
        {
            MUSTTAIL return unsupported_truthiness_error(ARGS);
        }

        pc += 3;
        if(accumulator.get_ptr<Float>()->value != 0.0)
        {
            pc += rel_target;
        }

        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_jump_if_false_float_truthiness(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        if(!can_convert_to<Float>(accumulator))
        {
            MUSTTAIL return unsupported_truthiness_error(ARGS);
        }

        pc += 3;
        if(accumulator.get_ptr<Float>()->value == 0.0)
        {
            pc += rel_target;
        }

        START(0);
        COMPLETE();
    }

    static ALWAYSINLINE bool try_get_exact_float(Value value, double *out)
    {
        if(can_convert_to<Float>(value))
        {
            *out = value.get_ptr<Float>()->value;
            return true;
        }
        return false;
    }

    static ALWAYSINLINE bool try_get_float_or_smi_or_bool(Value value,
                                                          double *out)
    {
        if(unlikely((value.as.integer & value_not_smi_or_boolean_mask) == 0))
        {
            Value integer_value;
            integer_value.as.integer =
                value.as.integer & value_boolean_to_integer_mask;
            *out = static_cast<double>(integer_value.get_smi());
            return true;
        }
        return try_get_exact_float(value, out);
    }

    static ALWAYSINLINE bool try_get_smi_or_bool(Value value, int64_t *out)
    {
        if(unlikely((value.as.integer & value_not_smi_or_boolean_mask) == 0))
        {
            Value integer_value;
            integer_value.as.integer =
                value.as.integer & value_boolean_to_integer_mask;
            *out = integer_value.get_smi();
            return true;
        }
        return false;
    }

    static ALWAYSINLINE int64_t floor_div_smi_values(int64_t left,
                                                     int64_t right)
    {
        int64_t quotient = left / right;
        int64_t remainder = left % right;
        if(remainder != 0 && ((remainder < 0) != (right < 0)))
        {
            quotient -= 1;
        }
        return quotient;
    }

    static ALWAYSINLINE int64_t modulo_smi_values(int64_t left, int64_t right)
    {
        int64_t remainder = left % right;
        if(remainder != 0 && ((remainder < 0) != (right < 0)))
        {
            remainder += right;
        }
        return remainder;
    }

    NOINLINE static INTERP_CC Value op_floordiv(PARAMS)
    {
        START_BINARY_REG_ACC();

        int64_t left_int;
        int64_t right_int;
        if(try_get_smi_or_bool(a, &left_int) &&
           try_get_smi_or_bool(b, &right_int))
        {
            if(unlikely(right_int == 0))
            {
                MUSTTAIL return zero_division_error(ARGS);
            }
            if(unlikely(left_int == value_smi_min && right_int == -1))
            {
                MUSTTAIL return overflow_path(ARGS);
            }
            accumulator =
                Value::from_smi(floor_div_smi_values(left_int, right_int));
            COMPLETE();
        }

        double left;
        double right;
        if(!try_get_float_or_smi_or_bool(a, &left) ||
           !try_get_float_or_smi_or_bool(b, &right))
        {
            MUSTTAIL return unsupported_int_division_error(ARGS);
        }
        if(unlikely(right == 0.0))
        {
            MUSTTAIL return zero_division_error(ARGS);
        }

        accumulator = thread->make_object_value<Float>(std::floor(left / right))
                          .raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_floordiv_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();

        int64_t left_int;
        int64_t right_int = b.get_smi();
        if(try_get_smi_or_bool(a, &left_int))
        {
            if(unlikely(right_int == 0))
            {
                MUSTTAIL return zero_division_error(ARGS);
            }
            if(unlikely(left_int == value_smi_min && right_int == -1))
            {
                MUSTTAIL return overflow_path(ARGS);
            }
            accumulator =
                Value::from_smi(floor_div_smi_values(left_int, right_int));
            COMPLETE();
        }

        double left;
        if(!try_get_exact_float(a, &left))
        {
            MUSTTAIL return unsupported_int_division_error(ARGS);
        }
        if(unlikely(right_int == 0))
        {
            MUSTTAIL return zero_division_error(ARGS);
        }

        accumulator =
            thread->make_object_value<Float>(std::floor(left / right_int))
                .raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_mod(PARAMS)
    {
        START_BINARY_REG_ACC();

        int64_t left_int;
        int64_t right_int;
        if(try_get_smi_or_bool(a, &left_int) &&
           try_get_smi_or_bool(b, &right_int))
        {
            if(unlikely(right_int == 0))
            {
                MUSTTAIL return zero_division_error(ARGS);
            }
            accumulator =
                Value::from_smi(modulo_smi_values(left_int, right_int));
            COMPLETE();
        }

        double left;
        double right;
        if(!try_get_float_or_smi_or_bool(a, &left) ||
           !try_get_float_or_smi_or_bool(b, &right))
        {
            MUSTTAIL return unsupported_modulo_error(ARGS);
        }
        if(unlikely(right == 0.0))
        {
            MUSTTAIL return zero_division_error(ARGS);
        }

        double result = std::fmod(left, right);
        if(result != 0.0 && (std::signbit(result) != std::signbit(right)))
        {
            result += right;
        }
        else if(result == 0.0)
        {
            result = std::copysign(0.0, right);
        }
        accumulator = thread->make_object_value<Float>(result).raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_mod_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();

        int64_t left_int;
        int64_t right_int = b.get_smi();
        if(try_get_smi_or_bool(a, &left_int))
        {
            if(unlikely(right_int == 0))
            {
                MUSTTAIL return zero_division_error(ARGS);
            }
            accumulator =
                Value::from_smi(modulo_smi_values(left_int, right_int));
            COMPLETE();
        }

        double left;
        if(!try_get_exact_float(a, &left))
        {
            MUSTTAIL return unsupported_modulo_error(ARGS);
        }
        if(unlikely(right_int == 0))
        {
            MUSTTAIL return zero_division_error(ARGS);
        }

        double result = std::fmod(left, right_int);
        if(result != 0.0 &&
           (std::signbit(result) != std::signbit(double(right_int))))
        {
            result += right_int;
        }
        else if(result == 0.0)
        {
            result = std::copysign(0.0, double(right_int));
        }
        accumulator = thread->make_object_value<Float>(result).raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_sub_float(PARAMS)
    {
        START_BINARY_REG_ACC();

        double left;
        double right;
        if(!try_get_float_or_smi_or_bool(a, &left) ||
           !try_get_float_or_smi_or_bool(b, &right))
        {
            MUSTTAIL return unsupported_subtraction_error(ARGS);
        }

        accumulator =
            thread->make_object_value<Float>(left - right).raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_sub_smi_float(PARAMS)
    {
        START_BINARY_ACC_SMI();

        double left;
        if(!try_get_exact_float(a, &left))
        {
            MUSTTAIL return unsupported_subtraction_error(ARGS);
        }

        accumulator =
            thread->make_object_value<Float>(left - double(b.get_smi()))
                .raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_mul_float(PARAMS)
    {
        START_BINARY_REG_ACC();

        double left;
        double right;
        if(!try_get_float_or_smi_or_bool(a, &left) ||
           !try_get_float_or_smi_or_bool(b, &right))
        {
            MUSTTAIL return unsupported_multiplication_error(ARGS);
        }

        accumulator =
            thread->make_object_value<Float>(left * right).raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_mul_smi_float(PARAMS)
    {
        START_BINARY_ACC_SMI();

        double left;
        if(!try_get_exact_float(a, &left))
        {
            MUSTTAIL return unsupported_multiplication_error(ARGS);
        }

        accumulator =
            thread->make_object_value<Float>(left * double(b.get_smi()))
                .raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_truediv(PARAMS)
    {
        START_BINARY_REG_ACC();

        double left;
        double right;
        if(!try_get_float_or_smi_or_bool(a, &left) ||
           !try_get_float_or_smi_or_bool(b, &right))
        {
            MUSTTAIL return unsupported_division_error(ARGS);
        }
        if(unlikely(right == 0.0))
        {
            MUSTTAIL return zero_division_error(ARGS);
        }

        accumulator =
            thread->make_object_value<Float>(left / right).raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_negate_float_arithmetic(PARAMS)
    {
        START_UNARY_ACC();

        double value;
        if(!try_get_exact_float(a, &value))
        {
            MUSTTAIL return unsupported_unary_arithmetic_error(ARGS);
        }

        accumulator = thread->make_object_value<Float>(-value).raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_plus_float_arithmetic(PARAMS)
    {
        START_UNARY_ACC();

        double value;
        if(!try_get_exact_float(a, &value))
        {
            MUSTTAIL return unsupported_unary_arithmetic_error(ARGS);
        }

        accumulator = thread->make_object_value<Float>(value).raw_value();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_sqrt_type_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"TypeError",
            L"sqrt() argument must be int or float");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_sqrt_domain_error(PARAMS)
    {
        ExceptionalTarget target = set_builtin_exception_and_resolve_frame_exit(
            thread, fp, pc, code_object, L"ValueError", L"math domain error");
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    static ALWAYSINLINE void enter_function_frame_at_new_fp(
        Value *&fp, const uint8_t *&pc, CodeObject *&code_object,
        TValue<Function> fun, Value *new_fp, uint32_t instr_len)
    {
        assert(is_stack_frame_aligned(new_fp));
        pc += instr_len;

        initialize_frame_header(new_fp, fp, code_object, pc);

        fp = new_fp;
        code_object = fun.extract()->code_object.extract();
        pc = code_object->code.data();
    }

    static ALWAYSINLINE void populate_function_call_cache_with_guard(
        FunctionCallInlineCache &cache, Value guard_value, TValue<Function> fun,
        ValidityCell *validity_cell, uint32_t n_args)
    {
        cache.guard_value = guard_value;
        cache.function = fun.extract();
        cache.code_object = fun.extract()->code_object.extract();
        cache.validity_cell = validity_cell;
        cache.n_args = n_args;
        cache.adaptation =
            function_call_adaptation_for_positional_call(fun, n_args);
    }

    static ALWAYSINLINE void populate_operator_call_cache(
        OperatorInlineCache &cache, TValue<Function> fun, uint32_t n_args,
        bool has_self, FunctionCallAdaptation adaptation,
        TrustedHandler handler)
    {
        cache.handler = handler;
        cache.function = fun.extract();
        cache.code_object = fun.extract()->code_object.extract();
        cache.n_args = n_args;
        cache.adaptation = adaptation;
        cache.has_self = has_self;
    }

    static ALWAYSINLINE void
    populate_function_call_cache(FunctionCallInlineCache &cache,
                                 TValue<Function> fun, uint32_t n_args)
    {
        populate_function_call_cache_with_guard(cache, fun.raw_value(), fun,
                                                nullptr, n_args);
    }

    static ALWAYSINLINE void
    populate_constructor_call_cache(FunctionCallInlineCache &cache,
                                    ClassObject *cls, TValue<Function> thunk,
                                    ValidityCell *lookup_cell, uint32_t n_args)
    {
        populate_function_call_cache_with_guard(cache, Value::from_oop(cls),
                                                thunk, lookup_cell, n_args);
    }

    NOINLINE static Expected<void> populate_positional_call_cache_from_callable(
        Value callable, uint32_t n_args, FunctionCallInlineCache &cache)
    {
        if(unlikely(!callable.is_ptr()))
        {
            return Expected<void>::raise_exception(L"TypeError",
                                                   L"object is not callable");
        }

        Object *callable_object = callable.get_ptr();
        if(callable_object->native_layout_id() == NativeLayoutId::ClassObject)
        {
            ClassObject *cls = static_cast<ClassObject *>(callable_object);
            ConstructorThunkLookup constructor =
                CL_TRY(cls->get_or_create_constructor_thunk());
            if(unlikely(!constructor.is_found()))
            {
                return Expected<void>::raise_exception(
                    L"TypeError", L"object is not callable");
            }

            TValue<Function> thunk =
                TValue<Function>::from_oop(constructor.thunk);
            if(unlikely(!thunk.extract()->accepts_positional_only_call_arity(
                   n_args)))
            {
                return Expected<void>::raise_exception(
                    L"TypeError", L"wrong number of arguments");
            }
            populate_constructor_call_cache(cache, cls, thunk,
                                            constructor.lookup_cell, n_args);
            return Expected<void>::ok();
        }

        if(unlikely(callable_object->native_layout_id() !=
                    NativeLayoutId::Function))
        {
            return Expected<void>::raise_exception(L"TypeError",
                                                   L"object is not callable");
        }

        TValue<Function> function =
            TValue<Function>::from_value_assumed(callable);
        if(unlikely(
               !function.extract()->accepts_positional_only_call_arity(n_args)))
        {
            return Expected<void>::raise_exception(
                L"TypeError", L"wrong number of arguments");
        }

        populate_function_call_cache(cache, function, n_args);
        return Expected<void>::ok();
    }

    static ALWAYSINLINE bool
    function_call_cache_matches(const FunctionCallInlineCache &cache, Value fun,
                                uint32_t n_args)
    {
        if(cache.n_args != n_args || cache.guard_value != fun)
        {
            return false;
        }
        return cache.validity_cell == nullptr ||
               cache.validity_cell->is_valid();
    }

    static ALWAYSINLINE bool
    keyword_call_cache_matches(const KeywordCallInlineCache &cache, Value fun,
                               Value keyword_names, uint32_t n_pos_args)
    {
        if(cache.n_pos_args != n_pos_args || cache.guard_value != fun ||
           cache.keyword_names != keyword_names)
        {
            return false;
        }
        return cache.validity_cell == nullptr ||
               cache.validity_cell->is_valid();
    }

    static ALWAYSINLINE void populate_keyword_call_cache_with_guard(
        KeywordCallInlineCache &cache, Value guard_value, TValue<Function> fun,
        ValidityCell *validity_cell, Value keyword_names, uint32_t n_pos_args,
        uint32_t default_fill_start_slot, std::vector<int8_t> keyword_dest_regs)
    {
        cache.guard_value = guard_value;
        cache.function = fun.extract();
        cache.code_object = fun.extract()->code_object.extract();
        cache.validity_cell = validity_cell;
        cache.keyword_names = keyword_names;
        cache.n_pos_args = n_pos_args;
        cache.default_fill_start_slot = default_fill_start_slot;
        if(fun.extract()->has_varargs())
        {
            cache.adaptation = FunctionCallAdaptation::Varargs;
        }
        else if(fun.extract()->default_parameters.value().has_value())
        {
            cache.adaptation = FunctionCallAdaptation::Defaults;
        }
        else if(is_fixed_arity_function(fun))
        {
            cache.adaptation = FunctionCallAdaptation::FixedArity;
        }
        else
        {
            cache.adaptation = FunctionCallAdaptation::Defaults;
        }
        cache.keyword_dest_regs = std::move(keyword_dest_regs);
    }

    static uint32_t
    default_fill_start_slot_for_keyword_call(TValue<Function> fun,
                                             uint32_t n_pos_args)
    {
        uint32_t first_default_slot =
            fun.extract()->call_signature.function.first_default_slot;
        return n_pos_args > first_default_slot ? n_pos_args
                                               : first_default_slot;
    }

    static Expected<void> build_and_populate_keyword_call_cache(
        KeywordCallInlineCache &cache, Value guard_value, TValue<Function> fun,
        ValidityCell *validity_cell, Value keyword_names_value,
        uint32_t n_pos_args, uint32_t n_kw_args)
    {
        KeywordCallInlineCache candidate;
        Function *function = fun.extract();
        FunctionSignature signature = function->call_signature.function;
        if(n_pos_args > signature.n_positional_parameters &&
           !signature.has_varargs())
        {
            return Expected<void>::raise_exception(
                L"TypeError", L"wrong number of arguments");
        }

        TValue<Tuple> keyword_names =
            TValue<Tuple>::from_value_assumed(keyword_names_value);
        assert(keyword_names.extract()->size() == n_kw_args);

        std::vector<bool> filled(signature.n_parameters, false);
        uint32_t n_filled_by_position =
            n_pos_args < signature.n_positional_parameters
                ? n_pos_args
                : signature.n_positional_parameters;
        for(uint32_t idx = 0; idx < n_filled_by_position; ++idx)
        {
            filled[idx] = true;
        }

        std::vector<int8_t> keyword_dest_regs;
        keyword_dest_regs.reserve(n_kw_args);
        CodeObject *target_code_object = function->code_object.extract();
        FunctionKeywordRemap &remap =
            target_code_object->function_keyword_remap;
        for(uint32_t kw_idx = 0; kw_idx < n_kw_args; ++kw_idx)
        {
            Value keyword_name =
                keyword_names.extract()->item_unchecked(kw_idx);
            for(uint32_t prev_idx = 0; prev_idx < kw_idx; ++prev_idx)
            {
                if(keyword_names.extract()->item_unchecked(prev_idx) ==
                   keyword_name)
                {
                    return Expected<void>::raise_exception(
                        L"TypeError", L"invalid keyword argument");
                }
            }

            bool found = false;
            uint16_t parameter_idx = 0;
            for(size_t remap_idx = 0; remap_idx < remap.size(); ++remap_idx)
            {
                if(remap.name_at(remap_idx) == keyword_name)
                {
                    found = true;
                    parameter_idx = remap.parameter_index_at(remap_idx);
                    break;
                }
            }
            if(!found || parameter_idx >= signature.n_parameters)
            {
                return Expected<void>::raise_exception(
                    L"TypeError", L"invalid keyword argument");
            }
            if(filled[parameter_idx])
            {
                return Expected<void>::raise_exception(
                    L"TypeError", L"invalid keyword argument");
            }

            filled[parameter_idx] = true;
            keyword_dest_regs.push_back(
                target_code_object->encode_reg(parameter_idx));
        }

        for(uint32_t parameter_idx = 0; parameter_idx < signature.n_parameters;
            ++parameter_idx)
        {
            if(signature.has_varargs() &&
               parameter_idx == signature.n_positional_parameters)
            {
                continue;
            }
            if(filled[parameter_idx])
            {
                continue;
            }
            if(!function->has_default_for_parameter(parameter_idx))
            {
                return Expected<void>::raise_exception(
                    L"TypeError", L"wrong number of arguments");
            }
        }

        uint32_t default_fill_start_slot =
            default_fill_start_slot_for_keyword_call(fun, n_filled_by_position);
        populate_keyword_call_cache_with_guard(
            candidate, guard_value, fun, validity_cell, keyword_names_value,
            n_pos_args, default_fill_start_slot, std::move(keyword_dest_regs));
        cache = std::move(candidate);
        return Expected<void>::ok();
    }

    NOINLINE static Expected<void> populate_keyword_call_cache_from_callable(
        Value callable, Value keyword_names, uint32_t n_pos_args,
        uint32_t n_kw_args, KeywordCallInlineCache &cache)
    {
        if(unlikely(!callable.is_ptr()))
        {
            return Expected<void>::raise_exception(L"TypeError",
                                                   L"object is not callable");
        }

        Object *callable_object = callable.get_ptr();
        if(callable_object->native_layout_id() == NativeLayoutId::ClassObject)
        {
            ClassObject *cls = static_cast<ClassObject *>(callable_object);
            ConstructorThunkLookup constructor =
                CL_TRY(cls->get_or_create_constructor_thunk());
            if(unlikely(!constructor.is_found()))
            {
                return Expected<void>::raise_exception(
                    L"TypeError", L"object is not callable");
            }

            TValue<Function> thunk =
                TValue<Function>::from_oop(constructor.thunk);
            return build_and_populate_keyword_call_cache(
                cache, Value::from_oop(cls), thunk, constructor.lookup_cell,
                keyword_names, n_pos_args, n_kw_args);
        }

        if(unlikely(callable_object->native_layout_id() !=
                    NativeLayoutId::Function))
        {
            return Expected<void>::raise_exception(L"TypeError",
                                                   L"object is not callable");
        }

        TValue<Function> function =
            TValue<Function>::from_value_assumed(callable);
        return build_and_populate_keyword_call_cache(cache, callable, function,
                                                     nullptr, keyword_names,
                                                     n_pos_args, n_kw_args);
    }

    static ALWAYSINLINE void
    initialize_default_parameters_from_slot(Value *new_fp, TValue<Function> fun,
                                            uint32_t default_fill_start_slot)
    {
        Optional<TValue<Tuple>> maybe_defaults =
            fun.extract()->default_parameters.value();
        assert(maybe_defaults.has_value());
        TValue<Tuple> defaults = maybe_defaults.value();
        CodeObject *target_code_object = fun.extract()->code_object.extract();
        uint32_t first_default_slot =
            fun.extract()->call_signature.function.first_default_slot;
        uint32_t default_end_slot =
            first_default_slot + uint32_t(defaults.extract()->size());
        if(default_fill_start_slot < first_default_slot)
        {
            default_fill_start_slot = first_default_slot;
        }
        if(likely(default_fill_start_slot >= default_end_slot))
        {
            return;
        }
        for(uint32_t param_idx = default_fill_start_slot;
            param_idx < default_end_slot; ++param_idx)
        {
            uint32_t default_idx = param_idx - first_default_slot;
            assert(default_idx < defaults.extract()->size());
            new_fp[target_code_object->encode_reg(param_idx)] =
                defaults.extract()->item_unchecked(default_idx);
        }
    }

    static ALWAYSINLINE void
    initialize_missing_default_arguments(Value *new_fp, TValue<Function> fun,
                                         uint32_t n_args)
    {
        uint32_t n_supplied_positional_args =
            n_args < fun.extract()
                         ->call_signature.function.n_positional_parameters
                ? n_args
                : fun.extract()
                      ->call_signature.function.n_positional_parameters;
        initialize_default_parameters_from_slot(new_fp, fun,
                                                n_supplied_positional_args);
    }

    static ALWAYSINLINE TValue<Tuple>
    make_varargs_argument(ThreadState *thread, Value *new_fp,
                          TValue<Function> fun, uint32_t n_args)
    {
        uint32_t n_positional_parameters =
            fun.extract()->call_signature.function.n_positional_parameters;
        uint32_t n_extra_args = n_args > n_positional_parameters
                                    ? n_args - n_positional_parameters
                                    : 0;
        CodeObject *target_code_object = fun.extract()->code_object.extract();
        return Tuple::from_frame_arguments(
            thread, new_fp,
            target_code_object->encode_reg(n_positional_parameters),
            n_extra_args);
    }

    static ALWAYSINLINE void store_varargs_argument(Value *new_fp,
                                                    TValue<Function> fun,
                                                    TValue<Tuple> varargs_tuple)
    {
        uint32_t n_positional_parameters =
            fun.extract()->call_signature.function.n_positional_parameters;
        CodeObject *target_code_object = fun.extract()->code_object.extract();
        new_fp[target_code_object->encode_reg(n_positional_parameters)] =
            varargs_tuple.raw_value();
    }

    static ALWAYSINLINE Value *
    new_frame_pointer_from_first_arg(Value *fp, TValue<Function> fun,
                                     int32_t first_arg_reg)
    {
        int32_t new_fp_reg = first_arg_reg -
                             int32_t(fun.extract()
                                         ->code_object.extract()
                                         ->get_padded_n_parameters()) +
                             1 - FrameHeaderSizeAboveFp;
        return fp + new_fp_reg;
    }

    static ALWAYSINLINE Value *
    new_frame_pointer_from_first_arg(Value *fp, CodeObject *target_code_object,
                                     int32_t first_arg_reg)
    {
        int32_t new_fp_reg =
            first_arg_reg -
            int32_t(target_code_object->get_padded_n_parameters()) + 1 -
            FrameHeaderSizeAboveFp;
        return fp + new_fp_reg;
    }

    static ALWAYSINLINE void enter_code_object_frame_from_prepared_args(
        Value *&fp, const uint8_t *&pc, CodeObject *&code_object,
        CodeObject *target_code_object, int32_t first_arg_reg,
        uint32_t instr_len)
    {
        Value *new_fp = new_frame_pointer_from_first_arg(fp, target_code_object,
                                                         first_arg_reg);
        assert(is_stack_frame_aligned(new_fp));
        pc += instr_len;

        initialize_frame_header(new_fp, fp, code_object, pc);

        fp = new_fp;
        code_object = target_code_object;
        pc = code_object->code.data();
    }

    static ALWAYSINLINE void enter_function_frame_from_positional_args(
        ThreadState *thread, Value *&fp, const uint8_t *&pc,
        CodeObject *&code_object, TValue<Function> fun, int32_t first_arg_reg,
        uint32_t n_args, uint32_t instr_len, FunctionCallAdaptation adaptation)
    {
        Value *new_fp =
            new_frame_pointer_from_first_arg(fp, fun, first_arg_reg);
        if(likely(adaptation == FunctionCallAdaptation::FixedArity))
        {
            enter_function_frame_at_new_fp(fp, pc, code_object, fun, new_fp,
                                           instr_len);
            return;
        }
        if(adaptation == FunctionCallAdaptation::Defaults)
        {
            initialize_missing_default_arguments(new_fp, fun, n_args);
            enter_function_frame_at_new_fp(fp, pc, code_object, fun, new_fp,
                                           instr_len);
            return;
        }

        assert(adaptation == FunctionCallAdaptation::Varargs);
        TValue<Tuple> varargs_tuple =
            make_varargs_argument(thread, new_fp, fun, n_args);
        if(fun.extract()->default_parameters.value().has_value())
        {
            initialize_missing_default_arguments(new_fp, fun, n_args);
        }
        store_varargs_argument(new_fp, fun, varargs_tuple);
        enter_function_frame_at_new_fp(fp, pc, code_object, fun, new_fp,
                                       instr_len);
    }

    static ALWAYSINLINE void enter_fixed_positional_call_from_cache(
        Value *&fp, const uint8_t *&pc, CodeObject *&code_object,
        FunctionCallInlineCache &cache, int32_t first_arg_reg,
        uint32_t instr_len)
    {
        assert(cache.adaptation == FunctionCallAdaptation::FixedArity);
        TValue<Function> function = TValue<Function>::from_oop(cache.function);
        CodeObject *target_code_object =
            function.extract()->code_object.extract();
        const uint8_t *target_pc = target_code_object->code.data();
        int32_t new_fp_reg =
            first_arg_reg -
            int32_t(target_code_object->get_padded_n_parameters()) + 1 -
            FrameHeaderSizeAboveFp;
        Value *new_fp = fp + new_fp_reg;
        pc += instr_len;

        initialize_frame_header(new_fp, fp, code_object, pc);

        fp = new_fp;
        code_object = target_code_object;
        pc = target_pc;
    }

    static ALWAYSINLINE void enter_positional_call_from_cache(
        ThreadState *thread, Value *&fp, const uint8_t *&pc,
        CodeObject *&code_object, FunctionCallInlineCache &cache,
        int32_t first_arg_reg, uint32_t n_args, uint32_t instr_len)
    {
        TValue<Function> function = TValue<Function>::from_oop(cache.function);
        if(likely(cache.adaptation == FunctionCallAdaptation::FixedArity))
        {
            enter_fixed_positional_call_from_cache(fp, pc, code_object, cache,
                                                   first_arg_reg, instr_len);
            return;
        }

        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, n_args,
            instr_len, cache.adaptation);
    }

    static ALWAYSINLINE bool try_enter_cached_positional_call(
        ThreadState *thread, Value *&fp, const uint8_t *&pc,
        CodeObject *&code_object, Value callable, int32_t first_arg_reg,
        uint32_t n_args, uint32_t instr_len, FunctionCallInlineCache &cache)
    {
        if(unlikely(!function_call_cache_matches(cache, callable, n_args)))
        {
            return false;
        }

        enter_positional_call_from_cache(thread, fp, pc, code_object, cache,
                                         first_arg_reg, n_args, instr_len);
        return true;
    }

    static ALWAYSINLINE void enter_function_frame_from_keyword_args(
        ThreadState *thread, Value *&fp, const uint8_t *&pc,
        CodeObject *&code_object, KeywordCallInlineCache &cache,
        int32_t first_arg_reg, uint32_t n_pos_args, int32_t first_kw_value_reg,
        uint32_t n_kw_args, uint32_t instr_len)
    {
        TValue<Function> fun = TValue<Function>::from_oop(cache.function);
        Value *new_fp = new_frame_pointer_from_first_arg(fp, cache.code_object,
                                                         first_arg_reg);

        if(cache.adaptation == FunctionCallAdaptation::Varargs)
        {
            TValue<Tuple> varargs_tuple =
                make_varargs_argument(thread, new_fp, fun, n_pos_args);
            if(fun.extract()->default_parameters.value().has_value())
            {
                initialize_default_parameters_from_slot(
                    new_fp, fun, cache.default_fill_start_slot);
            }
            store_varargs_argument(new_fp, fun, varargs_tuple);
        }
        else if(cache.adaptation != FunctionCallAdaptation::FixedArity &&
                fun.extract()->default_parameters.value().has_value())
        {
            initialize_default_parameters_from_slot(
                new_fp, fun, cache.default_fill_start_slot);
        }

        assert(cache.keyword_dest_regs.size() == n_kw_args);
        for(uint32_t kw_idx = 0; kw_idx < n_kw_args; ++kw_idx)
        {
            new_fp[cache.keyword_dest_regs[kw_idx]] =
                fp[first_kw_value_reg - int32_t(kw_idx)];
        }

        enter_function_frame_at_new_fp(fp, pc, code_object, fun, new_fp,
                                       instr_len);
    }

    static ALWAYSINLINE int32_t prepare_method_call_argument_slots(
        Value *fp, int32_t receiver_reg, uint32_t n_user_args, Value self)
    {
        if(!self.is_not_present())
        {
            fp[receiver_reg] = self;
            return receiver_reg;
        }

        for(uint32_t arg_idx = 0; arg_idx < n_user_args; ++arg_idx)
        {
            int32_t target_reg = receiver_reg - int32_t(arg_idx);
            fp[target_reg] = fp[target_reg - 1];
        }
        return receiver_reg;
    }

    static ALWAYSINLINE DispatchTableEntry
    dispatch_entry_for_pc(void *dispatch, const uint8_t *pc)
    {
        return reinterpret_cast<DispatchTable *>(dispatch)->table[pc[0]];
    }

    static ALWAYSINLINE void
    resolve_operator_pending_exception(ThreadState *thread, Value *&fp,
                                       const uint8_t *&pc,
                                       CodeObject *&code_object)
    {
        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
    }

    static ALWAYSINLINE void enter_binary_operator_python_function(
        ThreadState *thread, Value *&fp, const uint8_t *&pc,
        CodeObject *&code_object, const OperatorWalkDescriptor &descriptor,
        OperatorDispatchTableId table_id, Value operand0, Value operand1,
        const uint8_t *continuation_pc)
    {
        const OperatorInlineCache &entry = descriptor.cache_entry;
        assert(entry.function != nullptr);
        assert(entry.n_args == 1 || entry.n_args == 2);

        int32_t prefix_reg = code_object->get_first_free_arg_encoded_reg();
        setup_binary_operator_continuation_prefix(fp, prefix_reg, table_id,
                                                  descriptor.resume_index,
                                                  operand0, operand1);
        bool reflected = operator_step_action_is_reflected(descriptor.action);
        int32_t first_arg_reg = prefix_reg - 4;
        Value self = Value::not_present();
        if(unlikely(reflected))
        {
            fp[first_arg_reg] = operand1;
            fp[first_arg_reg - 1] = operand0;
            if(likely(entry.has_self))
            {
                self = operand1;
            }
        }
        else
        {
            fp[first_arg_reg] = operand0;
            fp[first_arg_reg - 1] = operand1;
            if(likely(entry.has_self))
            {
                self = operand0;
            }
        }
        first_arg_reg =
            prepare_method_call_argument_slots(fp, first_arg_reg, 1, self);

        assert(continuation_pc >= pc);
        uint32_t continuation_instr_len = uint32_t(continuation_pc - pc);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object,
            TValue<Function>::from_oop(entry.function), first_arg_reg,
            entry.n_args, continuation_instr_len, entry.adaptation);
    }

    static ALWAYSINLINE void enter_cached_binary_operator_python_function(
        ThreadState *thread, Value *&fp, const uint8_t *&pc,
        CodeObject *&code_object, const OperatorInlineCache &entry,
        OperatorDispatchTableId table_id, Value operand0, Value operand1,
        const uint8_t *continuation_pc)
    {
        assert(entry.function != nullptr);
        assert(entry.n_args == 1 || entry.n_args == 2);
        assert(entry.resume_index != UINT32_MAX);

        int32_t prefix_reg = code_object->get_first_free_arg_encoded_reg();
        setup_binary_operator_continuation_prefix(
            fp, prefix_reg, table_id, entry.resume_index, operand0, operand1);
        int32_t first_arg_reg = prefix_reg - 4;
        Value self = Value::not_present();
        if(unlikely(entry.reflected_python_call))
        {
            fp[first_arg_reg] = operand1;
            fp[first_arg_reg - 1] = operand0;
            if(likely(entry.has_self))
            {
                self = operand1;
            }
        }
        else
        {
            fp[first_arg_reg] = operand0;
            fp[first_arg_reg - 1] = operand1;
            if(likely(entry.has_self))
            {
                self = operand0;
            }
        }
        first_arg_reg =
            prepare_method_call_argument_slots(fp, first_arg_reg, 1, self);

        assert(continuation_pc >= pc);
        uint32_t continuation_instr_len = uint32_t(continuation_pc - pc);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object,
            TValue<Function>::from_oop(entry.function), first_arg_reg,
            entry.n_args, continuation_instr_len, entry.adaptation);
    }

    static ALWAYSINLINE void install_operator_cache_if_cacheable(
        OperatorInlineCache *cache, const OperatorWalkDescriptor &descriptor)
    {
        if(cache != nullptr &&
           descriptor.cache_entry.operand_lookup_validity_cells[0] != nullptr)
        {
            *cache = descriptor.cache_entry;
        }
    }

    static ALWAYSINLINE DispatchTableEntry dispatch_binary_operator_walk_result(
        ThreadState *thread, Value *&fp, const uint8_t *&pc, void *dispatch,
        CodeObject *&code_object, Value &accumulator,
        OperatorDispatchTableId table_id, Value operand0, Value operand1,
        const OperatorWalkDescriptor &descriptor,
        const uint8_t *continuation_pc, const uint8_t *next_pc,
        OperatorInlineCache *cache)
    {
        switch(descriptor.status)
        {
            case OperatorWalkStatus::CallPythonFunction:
                install_operator_cache_if_cacheable(cache, descriptor);
                enter_binary_operator_python_function(
                    thread, fp, pc, code_object, descriptor, table_id, operand0,
                    operand1, continuation_pc);
                return dispatch_entry_for_pc(dispatch, pc);

            case OperatorWalkStatus::CallTrustedHandler:
                {
                    assert(descriptor.cache_entry.handler.arity ==
                           TrustedHandlerArity::Binary);
                    install_operator_cache_if_cacheable(cache, descriptor);
                    accumulator = descriptor.cache_entry.handler.binary(
                        thread, operand0, operand1);
                    assert(!accumulator.is_not_implemented_singleton());
                    if(unlikely(accumulator.is_exception_marker()))
                    {
                        resolve_operator_pending_exception(thread, fp, pc,
                                                           code_object);
                        return dispatch_entry_for_pc(dispatch, pc);
                    }
                    pc = next_pc;
                    return dispatch_entry_for_pc(dispatch, pc);
                }

            case OperatorWalkStatus::NativeResult:
                accumulator = descriptor.result;
                pc = next_pc;
                return dispatch_entry_for_pc(dispatch, pc);

            case OperatorWalkStatus::PropagatePendingException:
                resolve_operator_pending_exception(thread, fp, pc, code_object);
                return dispatch_entry_for_pc(dispatch, pc);
        }

        __builtin_unreachable();
    }

    static ALWAYSINLINE bool both_smi_or_bool(Value a, Value b)
    {
        return ((a.as.integer | b.as.integer) &
                value_not_smi_or_boolean_mask) == 0;
    }

    static ALWAYSINLINE int64_t smi_or_bool_comparison_value(Value value)
    {
        return value.as.integer & ~int64_t(value_boolean_tag);
    }

    static ALWAYSINLINE DispatchTableEntry
    dispatch_cached_reflectable_binary_operator(
        ThreadState *thread, Value *&fp, const uint8_t *&pc, void *dispatch,
        CodeObject *&code_object, Value &accumulator,
        OperatorDispatchTableId table_id, uint8_t cache_idx, Value operand0,
        Value operand1, const uint8_t *continuation_pc, const uint8_t *next_pc)
    {
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];
        if(likely(cache.matches_reflectable_binary(operand0, operand1)))
        {
            if(cache.handler.arity == TrustedHandlerArity::Binary)
            {
                accumulator = cache.handler.binary(thread, operand0, operand1);
                assert(!accumulator.is_not_implemented_singleton());
                if(unlikely(accumulator.is_exception_marker()))
                {
                    resolve_operator_pending_exception(thread, fp, pc,
                                                       code_object);
                    return dispatch_entry_for_pc(dispatch, pc);
                }
                pc = next_pc;
                return dispatch_entry_for_pc(dispatch, pc);
            }
            if(likely(cache.function != nullptr))
            {
                enter_cached_binary_operator_python_function(
                    thread, fp, pc, code_object, cache, table_id, operand0,
                    operand1, continuation_pc);
                return dispatch_entry_for_pc(dispatch, pc);
            }
        }

        OperatorWalkDescriptor descriptor = walk_operator_table(
            thread, table_id, 0, OperatorCacheability::CacheableMaybeReflected,
            operand0, operand1, Value::not_present());
        return dispatch_binary_operator_walk_result(
            thread, fp, pc, dispatch, code_object, accumulator, table_id,
            operand0, operand1, descriptor, continuation_pc, next_pc, &cache);
    }

    static ALWAYSINLINE DispatchTableEntry
    dispatch_binary_operator_from_continuation(
        ThreadState *thread, Value *&fp, const uint8_t *&pc, void *dispatch,
        CodeObject *&code_object, Value &accumulator,
        const uint8_t *continuation_pc, const uint8_t *next_pc)
    {
        int32_t prefix_reg = code_object->get_first_free_arg_encoded_reg();
        OperatorDispatchTableId table_id;
        uint32_t resume_index;
        read_operator_continuation_header(fp, prefix_reg, table_id,
                                          resume_index);
        Value operand0;
        Value operand1;
        read_binary_operator_continuation_operands(fp, prefix_reg, operand0,
                                                   operand1);

        OperatorWalkDescriptor descriptor = walk_operator_table(
            thread, table_id, resume_index, OperatorCacheability::Uncacheable,
            operand0, operand1, Value::not_present());
        return dispatch_binary_operator_walk_result(
            thread, fp, pc, dispatch, code_object, accumulator, table_id,
            operand0, operand1, descriptor, continuation_pc, next_pc, nullptr);
    }

    enum class AttributeLoadPlanStatus : uint8_t
    {
        Ready,
        Slow,
        RequiresDescriptorDispatch,
    };

    static ALWAYSINLINE Object *
    mutation_plan_storage_owner(Value receiver,
                                const AttributeMutationPlan &plan)
    {
        Object *storage_owner = plan.storage_owner;
        if(storage_owner == nullptr)
        {
            assert(receiver.is_ptr());
            storage_owner = receiver.get_ptr<Object>();
        }
        return storage_owner;
    }

    static ALWAYSINLINE AttributeLoadPlanStatus load_attr_from_plan_inline(
        Value receiver, const AttributeReadPlan &plan, Value &value_out)
    {
        switch(plan.kind)
        {
            case AttributeReadPlanKind::ConstantValue:
                value_out = plan.binding.self;
                return AttributeLoadPlanStatus::Ready;

            case AttributeReadPlanKind::ReceiverSlot:
                {
                    if(unlikely(plan.storage_location.kind !=
                                StorageKind::Inline))
                    {
                        value_out =
                            read_plan_storage_owner(receiver, plan)
                                ->read_storage_location(plan.storage_location);
                        return AttributeLoadPlanStatus::Ready;
                    }
                    const Object *storage_owner =
                        read_plan_storage_owner(receiver, plan);
                    value_out =
                        storage_owner->inline_slot_base()[plan.storage_location
                                                              .physical_idx];
                    return AttributeLoadPlanStatus::Ready;
                }

            case AttributeReadPlanKind::BindFunctionReceiver:
                {
                    if(unlikely(plan.storage_location.kind !=
                                StorageKind::Inline))
                    {
                        value_out =
                            read_plan_storage_owner(receiver, plan)
                                ->read_storage_location(plan.storage_location);
                        return AttributeLoadPlanStatus::Ready;
                    }
                    const Object *storage_owner =
                        read_plan_storage_owner(receiver, plan);
                    value_out =
                        storage_owner->inline_slot_base()[plan.storage_location
                                                              .physical_idx];
                    return AttributeLoadPlanStatus::Ready;
                }

            case AttributeReadPlanKind::DataDescriptorGet:
            case AttributeReadPlanKind::NonDataDescriptorGet:
                value_out = Value::not_present();
                return AttributeLoadPlanStatus::RequiresDescriptorDispatch;
        }

        __builtin_unreachable();
    }

    static ALWAYSINLINE bool store_attr_from_plan_inline_fast(
        Value receiver, const AttributeMutationPlan &plan, Value value)
    {
        if(unlikely(plan.kind != AttributeMutationPlanKind::StoreExisting))
        {
            return false;
        }
        Object *storage_owner = mutation_plan_storage_owner(receiver, plan);
        Shape *shape = storage_owner->get_shape();
        if(shape != nullptr && shape->has_flag(ShapeFlag::IsClassObject))
        {
            return false;
        }
        storage_owner->write_existing_storage_location(plan.storage_location(),
                                                       value);
        return true;
    }

    static ALWAYSINLINE bool store_attr_add_own_property_inline_fast(
        Value receiver, const AttributeMutationPlan &plan, Value value)
    {
        assert(plan.is_add_own_property());
        assert(receiver.is_ptr());
        assert(plan.next_shape != nullptr);
        assert(plan.storage_kind == StorageKind::Inline);
        Object *object = receiver.get_ptr<Object>();
        object->set_shape(plan.next_shape);
        object->write_storage_location(plan.storage_location(), value);
        return true;
    }

    static ALWAYSINLINE bool delete_attr_delete_own_property_inline_fast(
        Value receiver, const AttributeMutationPlan &plan)
    {
        assert(plan.is_delete_own_property());
        assert(receiver.is_ptr());
        assert(plan.next_shape != nullptr);
        Object *object = receiver.get_ptr<Object>();
        object->set_shape(plan.next_shape);
        object->write_storage_location(plan.storage_location(),
                                       Value::not_present());
        return true;
    }

    NOINLINE static INTERP_CC Value op_load_attr_cache_miss(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t const_offset = pc[2];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        TValue<String> attr_name = TValue<String>::from_value_assumed(
            code_object->constant_table[const_offset].value());
        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[cache_idx];
        AttributeReadDescriptor descriptor =
            resolve_attr_read_descriptor(receiver, attr_name);
        if(!descriptor.is_found())
        {
            MUSTTAIL return attribute_error(ARGS);
        }
        if(descriptor.is_cacheable())
        {
            cache.populate(receiver, descriptor);
        }
        AttributeLoadPlanStatus plan_status =
            load_attr_from_plan_inline(receiver, descriptor.plan, accumulator);
        if(unlikely(plan_status == AttributeLoadPlanStatus::Slow))
        {
            accumulator = load_attr_from_plan(receiver, descriptor.plan);
            if(unlikely(accumulator.is_not_present()))
            {
                MUSTTAIL return attribute_error(ARGS);
            }
        }
        if(unlikely(plan_status ==
                    AttributeLoadPlanStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_store_attr_cache_miss(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t const_offset = pc[2];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        TValue<String> attr_name = TValue<String>::from_value_assumed(
            code_object->constant_table[const_offset].value());
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        AttributeWriteDescriptor descriptor =
            resolve_attr_write_descriptor(receiver, attr_name);
        if(descriptor.is_found())
        {
            if(descriptor.is_cacheable())
            {
                cache.populate(receiver, descriptor);
            }
            if(unlikely(!store_attr_from_plan(receiver, descriptor.plan,
                                              accumulator)))
            {
                if(thread->has_pending_exception())
                {
                    MUSTTAIL return propagate_pending_exception(ARGS);
                }
                MUSTTAIL return attribute_assignment_error(ARGS);
            }
            COMPLETE();
        }
        if(descriptor.status == AttributeWriteStatus::NotFound &&
           receiver.is_ptr())
        {
            Object *receiver_object = receiver.get_ptr<Object>();
            Shape *receiver_shape = receiver_object->get_shape();
            if(receiver_shape != nullptr &&
               !receiver_shape->has_flag(ShapeFlag::IsClassObject) &&
               receiver_shape->allows_attribute_add_delete())
            {
                Shape *next_shape = receiver_shape->derive_transition(
                    attr_name, ShapeTransitionVerb::Add);
                StorageLocation storage_location =
                    next_shape->resolve_present_property(attr_name);
                assert(storage_location.is_found());
                if(storage_location.kind == StorageKind::Inline)
                {
                    AttributeMutationPlan plan =
                        AttributeMutationPlan::add_own_property(
                            next_shape, storage_location);
                    ValidityCell *lookup_validity_cell =
                        receiver_object->get_shape()
                            ->get_class()
                            ->get_or_create_mro_shape_and_contents_validity_cell();
                    cache.populate(receiver, plan, lookup_validity_cell);
                    store_attr_add_own_property_inline_fast(
                        receiver, cache.plan, accumulator);
                    COMPLETE();
                }
            }
            if(likely(
                   receiver_object->add_own_property(attr_name, accumulator)))
            {
                COMPLETE();
            }
        }
        MUSTTAIL return attribute_assignment_error(ARGS);
    }

    NOINLINE static INTERP_CC Value op_del_attr_cache_miss(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t const_offset = pc[2];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        TValue<String> attr_name = TValue<String>::from_value_assumed(
            code_object->constant_table[const_offset].value());
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        AttributeDeleteDescriptor descriptor =
            resolve_attr_delete_descriptor(receiver, attr_name);
        if(descriptor.is_found())
        {
            if(descriptor.is_cacheable())
            {
                cache.populate(receiver, descriptor);
            }
            delete_attr_from_plan(receiver, descriptor.plan);
            COMPLETE();
        }
        MUSTTAIL return attribute_error(ARGS);
    }

    static INTERP_CC Value op_lda_constant(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        accumulator = code_object->constant_table[const_offset];
        COMPLETE();
    }

    static INTERP_CC Value op_lda_smi(PARAMS)
    {
        START(2);
        int8_t smi = pc[1];
        accumulator = Value::from_smi(smi);
        COMPLETE();
    }

    static INTERP_CC Value op_lda_true(PARAMS)
    {
        START(1);
        accumulator = Value::True();
        COMPLETE();
    }

    static INTERP_CC Value op_lda_false(PARAMS)
    {
        START(1);
        accumulator = Value::False();
        COMPLETE();
    }

    static INTERP_CC Value op_lda_none(PARAMS)
    {
        START(1);
        accumulator = Value::None();
        COMPLETE();
    }

    static INTERP_CC Value op_ldar(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        accumulator = fp[reg];
        COMPLETE();
    }

    static INTERP_CC Value op_load_local_checked(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        accumulator = fp[reg];
        if(unlikely(accumulator.is_not_present()))
        {
            MUSTTAIL return local_name_error(ARGS);
        }
        COMPLETE();
    }

    static INTERP_CC Value op_clear_local(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        fp[reg] = Value::not_present();
        COMPLETE();
    }

    static INTERP_CC Value op_star(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        fp[reg] = accumulator;

        COMPLETE();
    }

    static INTERP_CC Value op_mov(PARAMS)
    {
        START(3);
        int8_t dst_reg = pc[1];
        int8_t src_reg = pc[2];
        fp[dst_reg] = fp[src_reg];

        COMPLETE();
    }

    static INTERP_CC Value op_del_local(PARAMS)
    {
        START(2);
        int8_t reg = pc[1];
        if(unlikely(fp[reg].is_not_present()))
        {
            MUSTTAIL return local_name_error(ARGS);
        }
        fp[reg] = Value::not_present();
        COMPLETE();
    }

#define LDAR_STAR_FASTPATH(idx)                                                \
    static INTERP_CC Value op_ldar##idx(PARAMS)                                \
    {                                                                          \
        START(1);                                                              \
        int8_t reg = -idx - cl::FrameHeaderSizeBelowFp - 1;                    \
        accumulator = fp[reg];                                                 \
        COMPLETE();                                                            \
    }                                                                          \
    static INTERP_CC Value op_star##idx(PARAMS)                                \
    {                                                                          \
        START(1);                                                              \
        int8_t reg = -idx - cl::FrameHeaderSizeBelowFp - 1;                    \
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

    static ALWAYSINLINE Value
    load_module_global_slot_from_plan_inline(const ModuleGlobalSlotPlan &plan)
    {
        assert(plan.storage_owner != nullptr);
        return plan.storage_owner->read_storage_location(plan.storage_location);
    }

    static ALWAYSINLINE HeapObject *
    store_module_global_from_store_existing_plan_inline_fast(
        const ModuleGlobalStoreExistingPlan &plan, Value value)
    {
        assert(plan.storage_owner != nullptr);
        return plan.storage_owner
            ->write_existing_storage_location_returning_zero_ref(
                plan.storage_location(), value);
    }

    NOINLINE static INTERP_CC Value op_lda_global_cache_miss(PARAMS)
    {
        START(3);
        uint8_t name_idx = pc[1];
        uint8_t cache_idx = pc[2];
        TValue<String> name = TValue<String>::from_value_assumed(
            code_object->constant_table[name_idx].value());
        ModuleObject *module = code_object->get_defining_module().extract();
        ModuleGlobalReadInlineCache &cache =
            code_object->module_global_read_caches[cache_idx];
        ModuleGlobalReadDescriptor descriptor =
            resolve_module_global_read_descriptor(module, name);
        if(descriptor.is_cacheable())
        {
            cache.populate(descriptor);
        }
        accumulator = load_module_global_from_plan(descriptor.plan);
        if(unlikely(accumulator.is_not_present()))
        {
            MUSTTAIL return module_global_name_error(ARGS);
        }
        COMPLETE();
    }

    static INTERP_CC Value op_lda_global(PARAMS)
    {
        START(3);
        uint8_t cache_idx = pc[2];
        ModuleGlobalReadInlineCache &cache =
            code_object->module_global_read_caches[cache_idx];
        if(unlikely(!cache.matches()))
        {
            MUSTTAIL return op_lda_global_cache_miss(ARGS);
        }
        accumulator = load_module_global_slot_from_plan_inline(cache.slot);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_sta_global_cache_miss(PARAMS)
    {
        START(3);
        uint8_t name_idx = pc[1];
        uint8_t cache_idx = pc[2];
        TValue<String> name = TValue<String>::from_value_assumed(
            code_object->constant_table[name_idx].value());
        ModuleObject *module = code_object->get_defining_module().extract();
        ModuleGlobalMutationInlineCache &cache =
            code_object->module_global_mutation_caches[cache_idx];
        ModuleGlobalWriteDescriptor descriptor =
            resolve_module_global_write_descriptor(module, name);
        if(unlikely(!descriptor.is_found()))
        {
            MUSTTAIL return module_global_assignment_error(ARGS);
        }
        if(descriptor.is_cacheable())
        {
            cache.populate(descriptor);
        }
        bool stored =
            store_module_global_from_plan(module, descriptor.plan, accumulator);
        if(unlikely(!stored))
        {
            MUSTTAIL return module_global_assignment_error(ARGS);
        }
        COMPLETE();
    }

    static INTERP_CC Value op_sta_global(PARAMS)
    {
        START(3);
        uint8_t cache_idx = pc[2];
        ModuleGlobalMutationInlineCache &cache =
            code_object->module_global_mutation_caches[cache_idx];
        if(unlikely(!cache.matches()))
        {
            MUSTTAIL return op_sta_global_cache_miss(ARGS);
        }
        HeapObject *zct_object =
            store_module_global_from_store_existing_plan_inline_fast(
                cache.store_existing, accumulator);
        if(unlikely(zct_object != nullptr))
        {
            thread->add_to_zero_count_table_if_needed(zct_object);
        }
        COMPLETE();
    }

    static INTERP_CC Value op_del_global(PARAMS)
    {
        START(2);
        uint8_t name_idx = pc[1];
        TValue<String> name = TValue<String>::from_value_assumed(
            code_object->constant_table[name_idx].value());
        ModuleObject *module = code_object->get_defining_module().extract();
        ModuleGlobalDeleteDescriptor descriptor =
            resolve_module_global_delete_descriptor(module, name);
        if(unlikely(!descriptor.is_found()))
        {
            MUSTTAIL return module_global_name_error(ARGS);
        }
        bool deleted = delete_module_global_from_plan(module, descriptor.plan);
        if(unlikely(!deleted))
        {
            MUSTTAIL return module_global_name_error(ARGS);
        }
        COMPLETE();
    }

    static INTERP_CC Value op_load_attr(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_load_attr_cache_miss(ARGS);
        }
        AttributeLoadPlanStatus plan_status =
            load_attr_from_plan_inline(receiver, cache.plan, accumulator);
        if(unlikely(plan_status ==
                    AttributeLoadPlanStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }
        if(unlikely(plan_status == AttributeLoadPlanStatus::Slow))
        {
            MUSTTAIL return op_load_attr_cache_miss(ARGS);
        }
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_store_attr_cached_slow(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        if(cache.plan.is_add_own_property())
        {
            store_attr_add_own_property_inline_fast(receiver, cache.plan,
                                                    accumulator);
            COMPLETE();
        }
        if(cache.plan.kind == AttributeMutationPlanKind::StoreExisting)
        {
            if(unlikely(
                   !store_attr_from_plan(receiver, cache.plan, accumulator)))
            {
                if(thread->has_pending_exception())
                {
                    MUSTTAIL return propagate_pending_exception(ARGS);
                }
                MUSTTAIL return attribute_assignment_error(ARGS);
            }
            COMPLETE();
        }
        MUSTTAIL return op_store_attr_cache_miss(ARGS);
    }

    static INTERP_CC Value op_store_attr(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_store_attr_cache_miss(ARGS);
        }
        if(unlikely(!store_attr_from_plan_inline_fast(receiver, cache.plan,
                                                      accumulator)))
        {
            MUSTTAIL return op_store_attr_cached_slow(ARGS);
        }
        COMPLETE();
    }

    static INTERP_CC Value op_del_attr(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[reg];
        AttributeMutationInlineCache &cache =
            code_object->attribute_mutation_caches[cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_del_attr_cache_miss(ARGS);
        }
        if(unlikely(!cache.plan.is_delete_own_property()))
        {
            MUSTTAIL return op_del_attr_cache_miss(ARGS);
        }
        delete_attr_delete_own_property_inline_fast(receiver, cache.plan);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_get_item_cache_miss(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 3;
        int8_t receiver_reg = pc[1];
        uint8_t cache_idx = pc[2];
        static constexpr uint32_t n_user_args = 1;
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        ShapeKey receiver_shape_key = ShapeKey::from_value(receiver);
        ShapeKey key_shape_key = ShapeKey::from_value(key);
        VirtualMachine *vm = thread->get_machine();
        TValue<String> method_name =
            vm->get_or_create_interned_string_value(L"__getitem__");
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];

        Value callable;
        Value self;
        AttributeReadDescriptor descriptor =
            resolve_special_method_read_descriptor(receiver, method_name);
        MethodCallTargetStatus target_status =
            prepare_method_call_target_from_descriptor(receiver, descriptor,
                                                       callable, self);
        bool can_cache_call = target_status == MethodCallTargetStatus::Ready &&
                              descriptor.is_cacheable();
        if(can_cache_call)
        {
            cache.populate_operand0_method_lookup_validity(receiver,
                                                           descriptor);
            cache.populate_binary_shapes(receiver_shape_key, key_shape_key);
        }
        if(unlikely(target_status == MethodCallTargetStatus::Missing))
        {
            MUSTTAIL return subscript_error(ARGS);
        }
        if(unlikely(target_status ==
                    MethodCallTargetStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);

        if(unlikely(!callable.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        Object *fun_object = callable.get_ptr();
        if(unlikely(fun_object->native_layout_id() != NativeLayoutId::Function))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        TValue<Function> function =
            TValue<Function>::from_value_assumed(callable);
        if(unlikely(
               !function.extract()->accepts_positional_only_call_arity(n_args)))
        {
            MUSTTAIL return wrong_arity_error(ARGS);
        }
        FunctionCallAdaptation adaptation =
            function_call_adaptation_for_positional_call(function, n_args);
        TrustedHandler handler;
        if(can_cache_call)
        {
            CodeObject *target_code_object =
                function.extract()->code_object.extract();
            if(target_code_object->trusted_handler_resolver != nullptr)
            {
                TrustedHandler resolved_handler =
                    target_code_object->trusted_handler_resolver(
                        vm, receiver_shape_key, key_shape_key, ShapeKey{},
                        TrustedHandlerOperandOrder::Normal);
                if(resolved_handler.arity == TrustedHandlerArity::Binary)
                {
                    handler = resolved_handler;
                }
            }
            populate_operator_call_cache(cache, function, n_args, has_self,
                                         adaptation, handler);
        }

        if(handler.arity == TrustedHandlerArity::Binary)
        {
            accumulator = handler.binary(thread, receiver, key);
            if(unlikely(accumulator.is_exception_marker()))
            {
                MUSTTAIL return propagate_pending_exception(ARGS);
            }
            START(call_instr_len);
            COMPLETE();
        }

        int8_t first_arg_reg = code_object->get_first_free_arg_encoded_reg();
        fp[first_arg_reg] = receiver;
        fp[first_arg_reg - 1] = key;
        first_arg_reg = prepare_method_call_argument_slots(fp, first_arg_reg,
                                                           n_user_args, self);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, n_args,
            call_instr_len, adaptation);

        auto *next_dispatch_fun =
            reinterpret_cast<DispatchTable *>(dispatch)->table[pc[0]];
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_get_item_cached_call_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 3;
        int8_t receiver_reg = pc[1];
        uint8_t cache_idx = pc[2];
        static constexpr uint32_t n_user_args = 1;
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];
        assert(cache.function != nullptr);

        Value self = cache.has_self ? receiver : Value::not_present();
        int8_t first_arg_reg = code_object->get_first_free_arg_encoded_reg();
        fp[first_arg_reg] = receiver;
        fp[first_arg_reg - 1] = key;
        first_arg_reg = prepare_method_call_argument_slots(fp, first_arg_reg,
                                                           n_user_args, self);
        TValue<Function> function = TValue<Function>::from_oop(cache.function);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, cache.n_args,
            call_instr_len, cache.adaptation);

        auto *next_dispatch_fun =
            reinterpret_cast<DispatchTable *>(dispatch)->table[pc[0]];
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    static INTERP_CC Value op_get_item(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 3;
        int8_t receiver_reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];
        if(unlikely(!cache.matches_binary(receiver, key)))
        {
            MUSTTAIL return op_get_item_cache_miss(ARGS);
        }
        if(cache.handler.arity == TrustedHandlerArity::Binary)
        {
            accumulator = cache.handler.binary(thread, receiver, key);
            if(unlikely(accumulator.is_exception_marker()))
            {
                MUSTTAIL return propagate_pending_exception(ARGS);
            }
            START(call_instr_len);
            COMPLETE();
        }
        if(unlikely(cache.function == nullptr))
        {
            MUSTTAIL return op_get_item_cache_miss(ARGS);
        }
        MUSTTAIL return op_get_item_cached_call_slow(ARGS);
    }

    NOINLINE static INTERP_CC Value op_set_item_cache_miss(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 4;
        int8_t receiver_reg = pc[1];
        int8_t value_reg = pc[2];
        uint8_t cache_idx = pc[3];
        static constexpr uint32_t n_user_args = 2;
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        Value value = fp[value_reg];
        ShapeKey receiver_shape_key = ShapeKey::from_value(receiver);
        ShapeKey key_shape_key = ShapeKey::from_value(key);
        ShapeKey value_shape_key = ShapeKey::from_value(value);
        VirtualMachine *vm = thread->get_machine();
        TValue<String> method_name =
            vm->get_or_create_interned_string_value(L"__setitem__");
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];

        Value callable;
        Value self;
        AttributeReadDescriptor descriptor =
            resolve_special_method_read_descriptor(receiver, method_name);
        MethodCallTargetStatus target_status =
            prepare_method_call_target_from_descriptor(receiver, descriptor,
                                                       callable, self);
        bool can_cache_call = target_status == MethodCallTargetStatus::Ready &&
                              descriptor.is_cacheable();
        if(can_cache_call)
        {
            cache.populate_operand0_method_lookup_validity(receiver,
                                                           descriptor);
            cache.populate_ternary_shapes(receiver_shape_key, key_shape_key,
                                          value_shape_key);
        }
        if(unlikely(target_status == MethodCallTargetStatus::Missing))
        {
            MUSTTAIL return subscript_assignment_error(ARGS);
        }
        if(unlikely(target_status ==
                    MethodCallTargetStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);

        if(unlikely(!callable.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        Object *fun_object = callable.get_ptr();
        if(unlikely(fun_object->native_layout_id() != NativeLayoutId::Function))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        TValue<Function> function =
            TValue<Function>::from_value_assumed(callable);
        if(unlikely(
               !function.extract()->accepts_positional_only_call_arity(n_args)))
        {
            MUSTTAIL return wrong_arity_error(ARGS);
        }
        FunctionCallAdaptation adaptation =
            function_call_adaptation_for_positional_call(function, n_args);
        TrustedHandler handler;
        if(can_cache_call)
        {
            CodeObject *target_code_object =
                function.extract()->code_object.extract();
            if(target_code_object->trusted_handler_resolver != nullptr)
            {
                TrustedHandler resolved_handler =
                    target_code_object->trusted_handler_resolver(
                        vm, receiver_shape_key, key_shape_key, value_shape_key,
                        TrustedHandlerOperandOrder::Normal);
                if(resolved_handler.arity == TrustedHandlerArity::Ternary)
                {
                    handler = resolved_handler;
                }
            }
            populate_operator_call_cache(cache, function, n_args, has_self,
                                         adaptation, handler);
        }

        if(handler.arity == TrustedHandlerArity::Ternary)
        {
            accumulator = handler.ternary(thread, receiver, key, value);
            if(unlikely(accumulator.is_exception_marker()))
            {
                MUSTTAIL return propagate_pending_exception(ARGS);
            }
            START(call_instr_len);
            COMPLETE();
        }

        int8_t first_arg_reg = code_object->get_first_free_arg_encoded_reg();
        fp[first_arg_reg] = receiver;
        fp[first_arg_reg - 1] = key;
        fp[first_arg_reg - 2] = value;
        first_arg_reg = prepare_method_call_argument_slots(fp, first_arg_reg,
                                                           n_user_args, self);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, n_args,
            call_instr_len, adaptation);

        auto *next_dispatch_fun =
            reinterpret_cast<DispatchTable *>(dispatch)->table[pc[0]];
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_set_item_cached_call_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 4;
        int8_t receiver_reg = pc[1];
        int8_t value_reg = pc[2];
        uint8_t cache_idx = pc[3];
        static constexpr uint32_t n_user_args = 2;
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        Value value = fp[value_reg];
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];
        assert(cache.function != nullptr);

        Value self = cache.has_self ? receiver : Value::not_present();
        int8_t first_arg_reg = code_object->get_first_free_arg_encoded_reg();
        fp[first_arg_reg] = receiver;
        fp[first_arg_reg - 1] = key;
        fp[first_arg_reg - 2] = value;
        first_arg_reg = prepare_method_call_argument_slots(fp, first_arg_reg,
                                                           n_user_args, self);
        TValue<Function> function = TValue<Function>::from_oop(cache.function);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, cache.n_args,
            call_instr_len, cache.adaptation);

        auto *next_dispatch_fun =
            reinterpret_cast<DispatchTable *>(dispatch)->table[pc[0]];
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    static INTERP_CC Value op_set_item(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 4;
        int8_t receiver_reg = pc[1];
        int8_t value_reg = pc[2];
        uint8_t cache_idx = pc[3];
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        Value value = fp[value_reg];
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];
        if(unlikely(!cache.matches_ternary(receiver, key, value)))
        {
            MUSTTAIL return op_set_item_cache_miss(ARGS);
        }
        if(cache.handler.arity == TrustedHandlerArity::Ternary)
        {
            accumulator = cache.handler.ternary(thread, receiver, key, value);
            if(unlikely(accumulator.is_exception_marker()))
            {
                MUSTTAIL return propagate_pending_exception(ARGS);
            }
            START(call_instr_len);
            COMPLETE();
        }
        if(unlikely(cache.function == nullptr))
        {
            MUSTTAIL return op_set_item_cache_miss(ARGS);
        }
        MUSTTAIL return op_set_item_cached_call_slow(ARGS);
    }

    NOINLINE static INTERP_CC Value op_del_item_cache_miss(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 3;
        int8_t receiver_reg = pc[1];
        uint8_t cache_idx = pc[2];
        static constexpr uint32_t n_user_args = 1;
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        ShapeKey receiver_shape_key = ShapeKey::from_value(receiver);
        ShapeKey key_shape_key = ShapeKey::from_value(key);
        VirtualMachine *vm = thread->get_machine();
        TValue<String> method_name =
            vm->get_or_create_interned_string_value(L"__delitem__");
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];

        Value callable;
        Value self;
        AttributeReadDescriptor descriptor =
            resolve_special_method_read_descriptor(receiver, method_name);
        MethodCallTargetStatus target_status =
            prepare_method_call_target_from_descriptor(receiver, descriptor,
                                                       callable, self);
        bool can_cache_call = target_status == MethodCallTargetStatus::Ready &&
                              descriptor.is_cacheable();
        if(can_cache_call)
        {
            cache.populate_operand0_method_lookup_validity(receiver,
                                                           descriptor);
            cache.populate_binary_shapes(receiver_shape_key, key_shape_key);
        }
        if(unlikely(target_status == MethodCallTargetStatus::Missing))
        {
            MUSTTAIL return subscript_deletion_error(ARGS);
        }
        if(unlikely(target_status ==
                    MethodCallTargetStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);

        if(unlikely(!callable.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        Object *fun_object = callable.get_ptr();
        if(unlikely(fun_object->native_layout_id() != NativeLayoutId::Function))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        TValue<Function> function =
            TValue<Function>::from_value_assumed(callable);
        if(unlikely(
               !function.extract()->accepts_positional_only_call_arity(n_args)))
        {
            MUSTTAIL return wrong_arity_error(ARGS);
        }
        FunctionCallAdaptation adaptation =
            function_call_adaptation_for_positional_call(function, n_args);
        TrustedHandler handler;
        if(can_cache_call)
        {
            CodeObject *target_code_object =
                function.extract()->code_object.extract();
            if(target_code_object->trusted_handler_resolver != nullptr)
            {
                TrustedHandler resolved_handler =
                    target_code_object->trusted_handler_resolver(
                        vm, receiver_shape_key, key_shape_key, ShapeKey{},
                        TrustedHandlerOperandOrder::Normal);
                if(resolved_handler.arity == TrustedHandlerArity::Binary)
                {
                    handler = resolved_handler;
                }
            }
            populate_operator_call_cache(cache, function, n_args, has_self,
                                         adaptation, handler);
        }

        if(handler.arity == TrustedHandlerArity::Binary)
        {
            accumulator = handler.binary(thread, receiver, key);
            if(unlikely(accumulator.is_exception_marker()))
            {
                MUSTTAIL return propagate_pending_exception(ARGS);
            }
            START(call_instr_len);
            COMPLETE();
        }

        int8_t first_arg_reg = code_object->get_first_free_arg_encoded_reg();
        fp[first_arg_reg] = receiver;
        fp[first_arg_reg - 1] = key;
        first_arg_reg = prepare_method_call_argument_slots(fp, first_arg_reg,
                                                           n_user_args, self);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, n_args,
            call_instr_len, adaptation);

        auto *next_dispatch_fun =
            reinterpret_cast<DispatchTable *>(dispatch)->table[pc[0]];
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_del_item_cached_call_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 3;
        int8_t receiver_reg = pc[1];
        uint8_t cache_idx = pc[2];
        static constexpr uint32_t n_user_args = 1;
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];
        assert(cache.function != nullptr);

        Value self = cache.has_self ? receiver : Value::not_present();
        int8_t first_arg_reg = code_object->get_first_free_arg_encoded_reg();
        fp[first_arg_reg] = receiver;
        fp[first_arg_reg - 1] = key;
        first_arg_reg = prepare_method_call_argument_slots(fp, first_arg_reg,
                                                           n_user_args, self);
        TValue<Function> function = TValue<Function>::from_oop(cache.function);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, cache.n_args,
            call_instr_len, cache.adaptation);

        auto *next_dispatch_fun =
            reinterpret_cast<DispatchTable *>(dispatch)->table[pc[0]];
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    static INTERP_CC Value op_del_item(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 3;
        int8_t receiver_reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value receiver = fp[receiver_reg];
        Value key = accumulator;
        OperatorInlineCache &cache = code_object->operator_caches[cache_idx];
        if(unlikely(!cache.matches_binary(receiver, key)))
        {
            MUSTTAIL return op_del_item_cache_miss(ARGS);
        }
        if(cache.handler.arity == TrustedHandlerArity::Binary)
        {
            accumulator = cache.handler.binary(thread, receiver, key);
            if(unlikely(accumulator.is_exception_marker()))
            {
                MUSTTAIL return propagate_pending_exception(ARGS);
            }
            START(call_instr_len);
            COMPLETE();
        }
        if(unlikely(cache.function == nullptr))
        {
            MUSTTAIL return op_del_item_cache_miss(ARGS);
        }
        MUSTTAIL return op_del_item_cached_call_slow(ARGS);
    }

    NOINLINE static INTERP_CC Value op_add_smi_dispatch(PARAMS)
    {
        uint8_t cache_idx = pc[2];
        Value a = accumulator;
        Value b = Value::from_smi(int8_t(pc[1]));
        DispatchTableEntry next_dispatch_fun =
            dispatch_cached_reflectable_binary_operator(
                thread, fp, pc, dispatch, code_object, accumulator,
                OperatorDispatchTableId::Add, cache_idx, a, b, pc + 3, pc + 4);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_add_dispatch(PARAMS)
    {
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value a = fp[reg];
        Value b = accumulator;
        DispatchTableEntry next_dispatch_fun =
            dispatch_cached_reflectable_binary_operator(
                thread, fp, pc, dispatch, code_object, accumulator,
                OperatorDispatchTableId::Add, cache_idx, a, b, pc + 3, pc + 4);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    static INTERP_CC Value op_add_smi(PARAMS)
    {
        START(4);
        Value a = accumulator;
        Value b = Value::from_smi(int8_t(pc[1]));
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return op_add_smi_dispatch(ARGS);
        }
        if(unlikely(__builtin_add_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer -= b.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static INTERP_CC Value op_add(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        Value a = fp[reg];
        Value b = accumulator;
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return op_add_dispatch(ARGS);
        }
        if(unlikely(__builtin_add_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer -= a.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static INTERP_CC Value op_sub_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return op_sub_smi_float(ARGS);
        }
        if(unlikely(__builtin_sub_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer += b.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static INTERP_CC Value op_sub(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return op_sub_float(ARGS);
        }
        if(unlikely(__builtin_sub_overflow(a.as.integer, b.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer += a.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static INTERP_CC Value op_mul(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return op_mul_float(ARGS);
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

    static INTERP_CC Value op_mul_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return op_mul_smi_float(ARGS);
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

    static INTERP_CC Value op_lshift(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return unsupported_shift_error(ARGS);
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

    static INTERP_CC Value op_lshift_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return unsupported_shift_error(ARGS);
        }
        int64_t shift_count = b.get_smi();
        // Bytecode invariant: codegen only emits LShiftSmi for shifts 0..63.
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

    static INTERP_CC Value op_rshift(PARAMS)
    {
        START_BINARY_REG_ACC();
        if(unlikely(A_OR_B_NOT_SMI()))
        {
            MUSTTAIL return unsupported_shift_error(ARGS);
        }

        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0))
            MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;

        COMPLETE();
    }

    static INTERP_CC Value op_rshift_smi(PARAMS)
    {
        START_BINARY_ACC_SMI();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return unsupported_shift_error(ARGS);
        }
        int64_t shift_count = b.get_smi();
        if(unlikely(shift_count < 0))
            MUSTTAIL return raise_value_error_negative_shift_count(ARGS);
        accumulator.as.integer = a.as.integer >> shift_count;
        accumulator.as.integer &= ~value_not_smi_mask;

        COMPLETE();
    }

    static INTERP_CC Value op_is(PARAMS)
    {
        START_BINARY_REG_ACC();

        accumulator =
            (a.as.integer == b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    static INTERP_CC Value op_is_not(PARAMS)
    {
        START_BINARY_REG_ACC();
        accumulator =
            (a.as.integer != b.as.integer) ? Value::True() : Value::False();
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_lt_dispatch(PARAMS)
    {
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value a = fp[reg];
        Value b = accumulator;
        DispatchTableEntry next_dispatch_fun =
            dispatch_cached_reflectable_binary_operator(
                thread, fp, pc, dispatch, code_object, accumulator,
                OperatorDispatchTableId::CompareLt, cache_idx, a, b, pc + 3,
                pc + 4);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_le_dispatch(PARAMS)
    {
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value a = fp[reg];
        Value b = accumulator;
        DispatchTableEntry next_dispatch_fun =
            dispatch_cached_reflectable_binary_operator(
                thread, fp, pc, dispatch, code_object, accumulator,
                OperatorDispatchTableId::CompareLe, cache_idx, a, b, pc + 3,
                pc + 4);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_ge_dispatch(PARAMS)
    {
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value a = fp[reg];
        Value b = accumulator;
        DispatchTableEntry next_dispatch_fun =
            dispatch_cached_reflectable_binary_operator(
                thread, fp, pc, dispatch, code_object, accumulator,
                OperatorDispatchTableId::CompareGe, cache_idx, a, b, pc + 3,
                pc + 4);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_gt_dispatch(PARAMS)
    {
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value a = fp[reg];
        Value b = accumulator;
        DispatchTableEntry next_dispatch_fun =
            dispatch_cached_reflectable_binary_operator(
                thread, fp, pc, dispatch, code_object, accumulator,
                OperatorDispatchTableId::CompareGt, cache_idx, a, b, pc + 3,
                pc + 4);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_eq_dispatch(PARAMS)
    {
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value a = fp[reg];
        Value b = accumulator;
        DispatchTableEntry next_dispatch_fun =
            dispatch_cached_reflectable_binary_operator(
                thread, fp, pc, dispatch, code_object, accumulator,
                OperatorDispatchTableId::CompareEq, cache_idx, a, b, pc + 3,
                pc + 4);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    NOINLINE static INTERP_CC Value op_ne_dispatch(PARAMS)
    {
        int8_t reg = pc[1];
        uint8_t cache_idx = pc[2];
        Value a = fp[reg];
        Value b = accumulator;
        DispatchTableEntry next_dispatch_fun =
            dispatch_cached_reflectable_binary_operator(
                thread, fp, pc, dispatch, code_object, accumulator,
                OperatorDispatchTableId::CompareNe, cache_idx, a, b, pc + 3,
                pc + 4);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    static INTERP_CC Value op_lt(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        Value a = fp[reg];
        Value b = accumulator;
        if(likely(both_smi_or_bool(a, b)))
        {
            accumulator = smi_or_bool_comparison_value(a) <
                                  smi_or_bool_comparison_value(b)
                              ? Value::True()
                              : Value::False();
            COMPLETE();
        }
        MUSTTAIL return op_lt_dispatch(ARGS);
    }

    static INTERP_CC Value op_le(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        Value a = fp[reg];
        Value b = accumulator;
        if(likely(both_smi_or_bool(a, b)))
        {
            accumulator = smi_or_bool_comparison_value(a) <=
                                  smi_or_bool_comparison_value(b)
                              ? Value::True()
                              : Value::False();
            COMPLETE();
        }
        MUSTTAIL return op_le_dispatch(ARGS);
    }

    static INTERP_CC Value op_ge(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        Value a = fp[reg];
        Value b = accumulator;
        if(likely(both_smi_or_bool(a, b)))
        {
            accumulator = smi_or_bool_comparison_value(a) >=
                                  smi_or_bool_comparison_value(b)
                              ? Value::True()
                              : Value::False();
            COMPLETE();
        }
        MUSTTAIL return op_ge_dispatch(ARGS);
    }

    static INTERP_CC Value op_gt(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        Value a = fp[reg];
        Value b = accumulator;
        if(likely(both_smi_or_bool(a, b)))
        {
            accumulator = smi_or_bool_comparison_value(a) >
                                  smi_or_bool_comparison_value(b)
                              ? Value::True()
                              : Value::False();
            COMPLETE();
        }
        MUSTTAIL return op_gt_dispatch(ARGS);
    }

    static INTERP_CC Value op_eq(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        Value a = fp[reg];
        Value b = accumulator;
        if(likely(!a.is_ptr() && !b.is_ptr()))
        {
            // See if we have a bit difference after clearing the bit that
            // promotes booleans to 0/1 integers.
            uint64_t difference =
                (a.as.integer ^ b.as.integer) & value_boolean_to_integer_mask;
            accumulator = (difference == 0) ? Value::True() : Value::False();
            COMPLETE();
        }
        MUSTTAIL return op_eq_dispatch(ARGS);
    }

    static INTERP_CC Value op_ne(PARAMS)
    {
        START(4);
        int8_t reg = pc[1];
        Value a = fp[reg];
        Value b = accumulator;
        if(likely(!a.is_ptr() && !b.is_ptr()))
        {
            // See if we have a bit difference after clearing the bit that
            // promotes booleans to 0/1 integers.
            uint64_t difference =
                (a.as.integer ^ b.as.integer) & value_boolean_to_integer_mask;
            accumulator = (difference != 0) ? Value::True() : Value::False();
            COMPLETE();
        }
        MUSTTAIL return op_ne_dispatch(ARGS);
    }

    NOINLINE static INTERP_CC Value op_check_operator_not_implemented(PARAMS)
    {
        if(!accumulator.is_not_implemented_singleton())
        {
            START(1);
            COMPLETE();
        }

        DispatchTableEntry next_dispatch_fun =
            dispatch_binary_operator_from_continuation(
                thread, fp, pc, dispatch, code_object, accumulator, pc, pc + 1);
        MUSTTAIL return next_dispatch_fun(ARGS);
    }

    static INTERP_CC Value op_negate(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return op_negate_float_arithmetic(ARGS);
        }

        if(unlikely(__builtin_sub_overflow(0, a.as.integer,
                                           &accumulator.as.integer)))
        {
            accumulator.as.integer = -accumulator.as.integer;
            MUSTTAIL return overflow_path(ARGS);
        }

        COMPLETE();
    }

    static INTERP_CC Value op_plus(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely(A_NOT_SMI()))
        {
            MUSTTAIL return op_plus_float_arithmetic(ARGS);
        }

        COMPLETE();
    }

    static INTERP_CC Value op_sqrt(PARAMS)
    {
        START_UNARY_ACC();

        double value;
        if(a.is_smi())
        {
            value = static_cast<double>(a.get_smi());
        }
        else if(can_convert_to<Float>(a))
        {
            value = a.get_ptr<Float>()->value;
        }
        else
        {
            MUSTTAIL return op_sqrt_type_error(ARGS);
        }

        if(unlikely(value < 0.0))
        {
            MUSTTAIL return op_sqrt_domain_error(ARGS);
        }

        accumulator =
            thread->make_object_value<Float>(std::sqrt(value)).raw_value();
        COMPLETE();
    }

    static INTERP_CC Value op_write_stdout(PARAMS)
    {
        START(1);
        if(!can_convert_to<String>(accumulator))
        {
            accumulator = thread->set_pending_builtin_exception_string(
                L"TypeError", L"__clover_write_stdout__ expects str");
            MUSTTAIL return propagate_pending_exception(ARGS);
        }

        thread->get_machine()->write_stdout(
            TValue<String>::from_value_assumed(accumulator));
        accumulator = Value::None();
        COMPLETE();
    }

    static INTERP_CC Value op_not(PARAMS)
    {
        START_UNARY_ACC();
        if(unlikely((a.as.integer & value_ptr_mask) != 0))
        {
            MUSTTAIL return op_not_float_truthiness(ARGS);
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

    static INTERP_CC Value op_create_function(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        TValue<CodeObject> code_obj = TValue<CodeObject>::from_value_assumed(
            code_object->constant_table[const_offset].value());

        accumulator = thread
                          ->make_object_value<Function>(
                              code_obj, code_obj.extract()->docstring.value())
                          .raw_value();

        COMPLETE();
    }

    static INTERP_CC Value op_create_function_with_defaults(PARAMS)
    {
        START(3);
        uint8_t const_offset = pc[1];
        int8_t defaults_reg = pc[2];
        TValue<CodeObject> code_obj = TValue<CodeObject>::from_value_assumed(
            code_object->constant_table[const_offset].value());
        TValue<Tuple> defaults =
            TValue<Tuple>::from_value_assumed(fp[defaults_reg]);

        accumulator = thread
                          ->make_object_value<Function>(
                              code_obj, code_obj.extract()->docstring.value(),
                              Optional<TValue<Tuple>>::some(defaults))
                          .raw_value();

        COMPLETE();
    }

    static INTERP_CC Value op_create_instance_known_class(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        ClassObject *cls = assume_convert_to<ClassObject>(
            code_object->constant_table[const_offset].value());
        accumulator = Value::from_oop(thread->make_internal_raw<Instance>(cls));

        COMPLETE();
    }

    static INTERP_CC Value op_is_instance_of_known_class(PARAMS)
    {
        START(2);
        uint8_t const_offset = pc[1];
        ClassObject *target_class = assume_convert_to<ClassObject>(
            code_object->constant_table[const_offset].value());
        ClassObject *actual_class = thread->class_of_value(accumulator);
        accumulator = actual_class == target_class ||
                              is_subclass_of(actual_class, target_class)
                          ? Value::True()
                          : Value::False();

        COMPLETE();
    }

    static INTERP_CC Value op_create_list(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t n_items = pc[2];

        TValue<List> list = thread->make_object_value<List>(n_items);
        for(uint8_t idx = 0; idx < n_items; ++idx)
        {
            list.extract()->set_item_unchecked(idx, fp[reg - int8_t(idx)]);
        }
        accumulator = list.raw_value();

        COMPLETE();
    }

    static INTERP_CC Value op_create_tuple(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t n_items = pc[2];

        TValue<Tuple> tuple = thread->make_object_value<Tuple>(n_items);
        for(uint8_t idx = 0; idx < n_items; ++idx)
        {
            tuple.extract()->initialize_item_unchecked(
                idx, fp[int32_t(reg) - int32_t(idx)]);
        }
        accumulator = tuple.raw_value();

        COMPLETE();
    }

    static INTERP_CC Value op_create_binary_slice(PARAMS)
    {
        START(2);
        int8_t start_reg = pc[1];

        accumulator =
            make_slice(thread, fp[start_reg], accumulator, Value::None())
                .raw_value();

        COMPLETE();
    }

    static INTERP_CC Value op_create_ternary_slice(PARAMS)
    {
        START(3);
        int8_t start_reg = pc[1];
        int8_t stop_reg = pc[2];

        accumulator =
            make_slice(thread, fp[start_reg], fp[stop_reg], accumulator)
                .raw_value();

        COMPLETE();
    }

    static INTERP_CC Value op_create_dict(PARAMS)
    {
        START(3);
        int8_t reg = pc[1];
        uint8_t n_items = pc[2];

        TValue<Dict> dict = thread->make_object_value<Dict>();
        for(uint8_t idx = 0; idx < n_items; ++idx)
        {
            Value key = fp[reg - int8_t(idx * 2)];
            Value value = fp[reg - int8_t(idx * 2 + 1)];
            dict.extract()->set_item(key, value);
        }
        accumulator = dict.raw_value();

        COMPLETE();
    }

    static INTERP_CC Value op_create_class(PARAMS)
    {
        static constexpr uint32_t create_class_instr_len = 3;
        uint8_t body_const_offset = pc[1];
        int8_t first_arg_reg = pc[2];
        TValue<CodeObject> body_code = TValue<CodeObject>::from_value_assumed(
            code_object->constant_table[body_const_offset].value());

        const uint8_t *return_pc = pc + create_class_instr_len;
        Value *new_fp =
            fp + first_arg_reg -
            int32_t(round_up_to_abi_alignment(ClassBodyParameterCount)) + 1 -
            FrameHeaderSizeAboveFp;
        assert(is_stack_frame_aligned(new_fp));
        initialize_frame_header(new_fp, fp, code_object, return_pc);
        initialize_class_body_frame(new_fp, body_code.extract());

        fp = new_fp;
        code_object = body_code.extract();
        pc = code_object->code.data();

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_build_class(PARAMS)
    {
        Expected<Value> built_class =
            build_class_from_frame(thread, fp, code_object);
        if(unlikely(built_class.has_exception()))
        {
            ExceptionalTarget target =
                resolve_exceptional_frame_exit(thread, fp, pc, code_object);
            fp = target.fp;
            code_object = target.code_object;
            pc = target.interpreted_pc;
            START(0);
            COMPLETE();
        }
        accumulator = built_class.value();

        restore_frame_header(fp, pc, code_object);

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_check_init_returned_none(PARAMS)
    {
        START(1);
        if(unlikely(accumulator != Value::None()))
        {
            MUSTTAIL return init_returned_non_none_error(ARGS);
        }

        COMPLETE();
    }

    static INTERP_CC Value op_jump(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        pc += 3;
        pc += rel_target;

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_jump_if_true(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        if(unlikely((accumulator.as.integer & value_ptr_mask) != 0))
        {
            MUSTTAIL return op_jump_if_true_float_truthiness(ARGS);
        }

        pc += 3;
        if(accumulator.is_truthy())
        {
            pc += rel_target;
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_jump_if_false(PARAMS)
    {
        int16_t rel_target = read_int16_le(&pc[1]);
        if(unlikely((accumulator.as.integer & value_ptr_mask) != 0))
        {
            MUSTTAIL return op_jump_if_false_float_truthiness(ARGS);
        }

        pc += 3;
        if(accumulator.is_falsy())
        {
            pc += rel_target;
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_raise_assertion_error(PARAMS)
    {
        MUSTTAIL return assertion_error(ARGS);
    }

    static INTERP_CC Value op_raise_assertion_error_with_message(PARAMS)
    {
        MUSTTAIL return assertion_error_with_message(ARGS);
    }

    static INTERP_CC Value op_raise_unwind(PARAMS)
    {
        MUSTTAIL return raise_unwind(ARGS);
    }

    static INTERP_CC Value op_raise_unwind_with_context(PARAMS)
    {
        MUSTTAIL return raise_unwind_with_context(ARGS);
    }

    static INTERP_CC Value op_raise_bare(PARAMS)
    {
        MUSTTAIL return raise_bare(ARGS);
    }

    NOINLINE static INTERP_CC Value op_call_positional_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 5;
        int8_t callable_reg = pc[1];
        int8_t first_arg_reg = pc[2];
        uint8_t n_args = pc[3];
        uint8_t cache_idx = pc[4];
        Value callable = fp[callable_reg];
        FunctionCallInlineCache &call_cache =
            code_object->function_call_caches[cache_idx];
        if(unlikely(!function_call_cache_matches(call_cache, callable, n_args)))
        {
            INTERP_TRY(populate_positional_call_cache_from_callable(
                callable, n_args, call_cache));
        }
        enter_positional_call_from_cache(thread, fp, pc, code_object,
                                         call_cache, first_arg_reg, n_args,
                                         call_instr_len);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_call_code_object(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 4;
        uint8_t const_offset = pc[1];
        int8_t first_arg_reg = pc[2];
        uint8_t n_args = pc[3];
        CodeObject *target_code_object = assume_convert_to<CodeObject>(
            code_object->constant_table[const_offset].value());
        assert(n_args == target_code_object->function_signature.n_parameters);
        (void)n_args;
        enter_code_object_frame_from_prepared_args(
            fp, pc, code_object, target_code_object, first_arg_reg,
            call_instr_len);

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_call_positional(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 5;
        int8_t callable_reg = pc[1];
        int8_t first_arg_reg = pc[2];
        uint8_t n_args = pc[3];
        uint8_t cache_idx = pc[4];
        Value fun = fp[callable_reg];
        FunctionCallInlineCache &cache =
            code_object->function_call_caches[cache_idx];

        if(unlikely(!function_call_cache_matches(cache, fun, n_args)))
        {
            MUSTTAIL return op_call_positional_slow(ARGS);
        }
        if(unlikely(cache.adaptation != FunctionCallAdaptation::FixedArity))
        {
            MUSTTAIL return op_call_positional_slow(ARGS);
        }
        TValue<Function> function = TValue<Function>::from_oop(cache.function);
        CodeObject *target_code_object =
            function.extract()->code_object.extract();
        const uint8_t *target_pc = target_code_object->code.data();
        Value *new_fp = new_frame_pointer_from_first_arg(fp, target_code_object,
                                                         first_arg_reg);
        pc += call_instr_len;

        initialize_frame_header(new_fp, fp, code_object, pc);

        fp = new_fp;
        code_object = target_code_object;
        pc = target_pc;
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_call_keyword_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 8;
        int8_t callable_reg = pc[1];
        int8_t first_arg_reg = pc[2];
        uint8_t n_pos_args = pc[3];
        int8_t first_kw_value_reg = pc[4];
        uint8_t n_kw_args = pc[5];
        uint8_t keyword_names_idx = pc[6];
        uint8_t cache_idx = pc[7];
        Value fun = fp[callable_reg];
        Value keyword_names =
            code_object->constant_table[keyword_names_idx].value();
        KeywordCallInlineCache &cache =
            code_object->keyword_call_caches[cache_idx];
        if(unlikely(!keyword_call_cache_matches(cache, fun, keyword_names,
                                                n_pos_args)))
        {
            INTERP_TRY(populate_keyword_call_cache_from_callable(
                fun, keyword_names, n_pos_args, n_kw_args, cache));
        }

        enter_function_frame_from_keyword_args(
            thread, fp, pc, code_object, cache, first_arg_reg, n_pos_args,
            first_kw_value_reg, n_kw_args, call_instr_len);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_call_keyword(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 8;
        int8_t callable_reg = pc[1];
        int8_t first_arg_reg = pc[2];
        uint8_t n_pos_args = pc[3];
        int8_t first_kw_value_reg = pc[4];
        uint8_t n_kw_args = pc[5];
        uint8_t keyword_names_idx = pc[6];
        uint8_t cache_idx = pc[7];
        Value fun = fp[callable_reg];
        Value keyword_names =
            code_object->constant_table[keyword_names_idx].value();
        KeywordCallInlineCache &cache =
            code_object->keyword_call_caches[cache_idx];

        if(unlikely(!keyword_call_cache_matches(cache, fun, keyword_names,
                                                n_pos_args)))
        {
            MUSTTAIL return op_call_keyword_slow(ARGS);
        }

        enter_function_frame_from_keyword_args(
            thread, fp, pc, code_object, cache, first_arg_reg, n_pos_args,
            first_kw_value_reg, n_kw_args, call_instr_len);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_call_method_attr_positional_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 6;
        int32_t receiver_reg = int8_t(pc[1]);
        uint8_t const_offset = pc[2];
        uint8_t read_cache_idx = pc[3];
        uint8_t call_cache_idx = pc[4];
        uint32_t n_user_args = uint8_t(pc[5]);
        Value receiver = fp[receiver_reg];
        TValue<String> attr_name = TValue<String>::from_value_assumed(
            code_object->constant_table[const_offset].value());

        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[read_cache_idx];
        Value callable;
        Value self;
        MethodCallTargetStatus target_status;
        if(cache.matches(receiver))
        {
            target_status = prepare_method_call_target_from_plan(
                receiver, cache.plan, callable, self);
        }
        else
        {
            AttributeReadDescriptor descriptor =
                resolve_attr_read_descriptor(receiver, attr_name);
            target_status = prepare_method_call_target_from_descriptor(
                receiver, descriptor, callable, self);
            if(target_status == MethodCallTargetStatus::Ready &&
               descriptor.is_cacheable())
            {
                cache.populate(receiver, descriptor);
            }
        }
        if(unlikely(target_status == MethodCallTargetStatus::Missing))
        {
            MUSTTAIL return method_lookup_error(ARGS);
        }
        if(unlikely(target_status ==
                    MethodCallTargetStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);
        FunctionCallInlineCache &call_cache =
            code_object->function_call_caches[call_cache_idx];
        int32_t first_arg_reg = prepare_method_call_argument_slots(
            fp, receiver_reg, n_user_args, self);
        if(unlikely(!try_enter_cached_positional_call(
               thread, fp, pc, code_object, callable, first_arg_reg, n_args,
               call_instr_len, call_cache)))
        {
            INTERP_TRY(populate_positional_call_cache_from_callable(
                callable, n_args, call_cache));
            enter_positional_call_from_cache(thread, fp, pc, code_object,
                                             call_cache, first_arg_reg, n_args,
                                             call_instr_len);
        }
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_call_method_attr_positional(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 6;
        int32_t receiver_reg = int8_t(pc[1]);
        uint8_t read_cache_idx = pc[3];
        uint8_t call_cache_idx = pc[4];
        uint32_t n_user_args = uint8_t(pc[5]);
        Value receiver = fp[receiver_reg];
        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[read_cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_call_method_attr_positional_slow(ARGS);
        }

        Value callable;
        Value self;
        MethodCallFastTargetStatus target_status =
            prepare_method_call_target_from_plan_fast(receiver, cache.plan,
                                                      callable, self);
        if(unlikely(target_status == MethodCallFastTargetStatus::Slow))
        {
            MUSTTAIL return op_call_method_attr_positional_slow(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);
        FunctionCallInlineCache &call_cache =
            code_object->function_call_caches[call_cache_idx];

        if(unlikely(!function_call_cache_matches(call_cache, callable, n_args)))
        {
            MUSTTAIL return op_call_method_attr_positional_slow(ARGS);
        }
        if(unlikely(call_cache.adaptation !=
                    FunctionCallAdaptation::FixedArity))
        {
            MUSTTAIL return op_call_method_attr_positional_slow(ARGS);
        }
        int32_t first_arg_reg = prepare_method_call_argument_slots(
            fp, receiver_reg, n_user_args, self);
        TValue<Function> function =
            TValue<Function>::from_oop(call_cache.function);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, n_args,
            call_instr_len, FunctionCallAdaptation::FixedArity);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_call_method_attr_keyword_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 9;
        int32_t receiver_reg = int8_t(pc[1]);
        uint8_t const_offset = pc[2];
        uint8_t read_cache_idx = pc[3];
        uint8_t call_cache_idx = pc[4];
        uint32_t n_user_pos_args = uint8_t(pc[5]);
        int8_t first_kw_value_reg = pc[6];
        uint8_t n_kw_args = pc[7];
        uint8_t keyword_names_idx = pc[8];
        Value receiver = fp[receiver_reg];
        TValue<String> attr_name = TValue<String>::from_value_assumed(
            code_object->constant_table[const_offset].value());
        Value keyword_names =
            code_object->constant_table[keyword_names_idx].value();

        AttributeReadInlineCache &attr_cache =
            code_object->attribute_read_caches[read_cache_idx];
        Value callable;
        Value self;
        MethodCallTargetStatus target_status;
        if(attr_cache.matches(receiver))
        {
            target_status = prepare_method_call_target_from_plan(
                receiver, attr_cache.plan, callable, self);
        }
        else
        {
            AttributeReadDescriptor descriptor =
                resolve_attr_read_descriptor(receiver, attr_name);
            target_status = prepare_method_call_target_from_descriptor(
                receiver, descriptor, callable, self);
            if(target_status == MethodCallTargetStatus::Ready &&
               descriptor.is_cacheable())
            {
                attr_cache.populate(receiver, descriptor);
            }
        }
        if(unlikely(target_status == MethodCallTargetStatus::Missing))
        {
            MUSTTAIL return method_lookup_error(ARGS);
        }
        if(unlikely(target_status ==
                    MethodCallTargetStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_pos_args = n_user_pos_args + (has_self ? 1 : 0);
        KeywordCallInlineCache &call_cache =
            code_object->keyword_call_caches[call_cache_idx];
        if(unlikely(!keyword_call_cache_matches(call_cache, callable,
                                                keyword_names, n_pos_args)))
        {
            INTERP_TRY(populate_keyword_call_cache_from_callable(
                callable, keyword_names, n_pos_args, n_kw_args, call_cache));
        }

        int32_t first_arg_reg = prepare_method_call_argument_slots(
            fp, receiver_reg, n_user_pos_args, self);
        enter_function_frame_from_keyword_args(
            thread, fp, pc, code_object, call_cache, first_arg_reg, n_pos_args,
            first_kw_value_reg, n_kw_args, call_instr_len);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_call_method_attr_keyword(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 9;
        int32_t receiver_reg = int8_t(pc[1]);
        uint8_t read_cache_idx = pc[3];
        uint8_t call_cache_idx = pc[4];
        uint32_t n_user_pos_args = uint8_t(pc[5]);
        int8_t first_kw_value_reg = pc[6];
        uint8_t n_kw_args = pc[7];
        uint8_t keyword_names_idx = pc[8];
        Value receiver = fp[receiver_reg];
        Value keyword_names =
            code_object->constant_table[keyword_names_idx].value();
        AttributeReadInlineCache &attr_cache =
            code_object->attribute_read_caches[read_cache_idx];
        if(unlikely(!attr_cache.matches(receiver)))
        {
            MUSTTAIL return op_call_method_attr_keyword_slow(ARGS);
        }

        Value callable;
        Value self;
        MethodCallFastTargetStatus target_status =
            prepare_method_call_target_from_plan_fast(receiver, attr_cache.plan,
                                                      callable, self);
        if(unlikely(target_status == MethodCallFastTargetStatus::Slow))
        {
            MUSTTAIL return op_call_method_attr_keyword_slow(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_pos_args = n_user_pos_args + (has_self ? 1 : 0);
        KeywordCallInlineCache &call_cache =
            code_object->keyword_call_caches[call_cache_idx];
        if(unlikely(!keyword_call_cache_matches(call_cache, callable,
                                                keyword_names, n_pos_args)))
        {
            MUSTTAIL return op_call_method_attr_keyword_slow(ARGS);
        }

        int32_t first_arg_reg = prepare_method_call_argument_slots(
            fp, receiver_reg, n_user_pos_args, self);
        enter_function_frame_from_keyword_args(
            thread, fp, pc, code_object, call_cache, first_arg_reg, n_pos_args,
            first_kw_value_reg, n_kw_args, call_instr_len);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_call_special_method_slow(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 8;
        int32_t receiver_reg = int8_t(pc[1]);
        uint8_t const_offset = pc[2];
        uint8_t read_cache_idx = pc[3];
        uint8_t call_cache_idx = pc[4];
        uint32_t n_user_args = uint8_t(pc[5]);
        uint8_t missing_exception_type_idx = pc[6];
        uint8_t missing_exception_message_idx = pc[7];
        Value receiver = fp[receiver_reg];
        TValue<String> method_name = TValue<String>::from_value_assumed(
            code_object->constant_table[const_offset].value());

        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[read_cache_idx];
        Value callable;
        Value self;
        MethodCallTargetStatus target_status;
        if(cache.matches(receiver))
        {
            target_status = prepare_method_call_target_from_plan(
                receiver, cache.plan, callable, self);
        }
        else
        {
            AttributeReadDescriptor descriptor =
                resolve_special_method_read_descriptor(receiver, method_name);
            target_status = prepare_method_call_target_from_descriptor(
                receiver, descriptor, callable, self);
            if(target_status == MethodCallTargetStatus::Ready &&
               descriptor.is_cacheable())
            {
                cache.populate(receiver, descriptor);
            }
        }
        if(unlikely(target_status == MethodCallTargetStatus::Missing))
        {
            TValue<ClassObject> exception_type =
                TValue<ClassObject>::from_value_assumed(
                    code_object->constant_table[missing_exception_type_idx]
                        .value());
            TValue<String> message = TValue<String>::from_value_assumed(
                code_object->constant_table[missing_exception_message_idx]
                    .value());
            (void)thread->set_pending_exception_string(exception_type, message);
            ExceptionalTarget target =
                resolve_exceptional_frame_exit(thread, fp, pc, code_object);
            fp = target.fp;
            code_object = target.code_object;
            pc = target.interpreted_pc;
            START(0);
            COMPLETE();
        }
        if(unlikely(target_status ==
                    MethodCallTargetStatus::RequiresDescriptorDispatch))
        {
            MUSTTAIL return descriptor_dispatch_error(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);

        if(unlikely(!callable.is_ptr()))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        Object *fun_object = callable.get_ptr();
        if(unlikely(fun_object->native_layout_id() != NativeLayoutId::Function))
        {
            MUSTTAIL return not_callable_error(ARGS);
        }

        TValue<Function> function =
            TValue<Function>::from_value_assumed(callable);
        FunctionCallInlineCache &call_cache =
            code_object->function_call_caches[call_cache_idx];
        if(function_call_cache_matches(call_cache, callable, n_args))
        {
            int32_t first_arg_reg = prepare_method_call_argument_slots(
                fp, receiver_reg, n_user_args, self);
            TValue<Function> cached_function =
                TValue<Function>::from_oop(call_cache.function);
            enter_function_frame_from_positional_args(
                thread, fp, pc, code_object, cached_function, first_arg_reg,
                n_args, call_instr_len, call_cache.adaptation);
            if(unlikely(thread->safepoint_requested()))
            {
                MUSTTAIL return op_committed_safepoint_slow(ARGS);
            }

            START(0);
            COMPLETE();
        }
        if(unlikely(
               !function.extract()->accepts_positional_only_call_arity(n_args)))
        {
            MUSTTAIL return wrong_arity_error(ARGS);
        }
        int32_t first_arg_reg = prepare_method_call_argument_slots(
            fp, receiver_reg, n_user_args, self);
        populate_function_call_cache(call_cache, function, n_args);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, n_args,
            call_instr_len, call_cache.adaptation);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        {
            START(0);
            COMPLETE();
        }
    }

    static INTERP_CC Value op_call_special_method(PARAMS)
    {
        static constexpr uint32_t call_instr_len = 8;
        int32_t receiver_reg = int8_t(pc[1]);
        uint8_t read_cache_idx = pc[3];
        uint8_t call_cache_idx = pc[4];
        uint32_t n_user_args = uint8_t(pc[5]);
        Value receiver = fp[receiver_reg];
        AttributeReadInlineCache &cache =
            code_object->attribute_read_caches[read_cache_idx];
        if(unlikely(!cache.matches(receiver)))
        {
            MUSTTAIL return op_call_special_method_slow(ARGS);
        }

        Value callable;
        Value self;
        MethodCallFastTargetStatus target_status =
            prepare_method_call_target_from_plan_fast(receiver, cache.plan,
                                                      callable, self);
        if(unlikely(target_status == MethodCallFastTargetStatus::Slow))
        {
            MUSTTAIL return op_call_special_method_slow(ARGS);
        }

        bool has_self = !self.is_not_present();
        uint32_t n_args = n_user_args + (has_self ? 1 : 0);
        FunctionCallInlineCache &call_cache =
            code_object->function_call_caches[call_cache_idx];

        if(unlikely(!function_call_cache_matches(call_cache, callable, n_args)))
        {
            MUSTTAIL return op_call_special_method_slow(ARGS);
        }

        if(unlikely(call_cache.adaptation !=
                    FunctionCallAdaptation::FixedArity))
        {
            MUSTTAIL return op_call_special_method_slow(ARGS);
        }

        int32_t first_arg_reg = prepare_method_call_argument_slots(
            fp, receiver_reg, n_user_args, self);
        TValue<Function> function =
            TValue<Function>::from_oop(call_cache.function);
        enter_function_frame_from_positional_args(
            thread, fp, pc, code_object, function, first_arg_reg, n_args,
            call_instr_len, FunctionCallAdaptation::FixedArity);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    static ALWAYSINLINE Value get_native_arg(Value *fp, CodeObject *code_object,
                                             uint32_t arg_idx)
    {
        int32_t reg = int32_t(code_object->get_padded_n_parameters()) - 1 +
                      FrameHeaderSizeAboveFp - int32_t(arg_idx);
        return fp[reg];
    }

    static INTERP_CC Value op_call_intrinsic0(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator =
            code_object->native_function_targets[target_idx].fixed0(thread);
        COMPLETE();
    }

    static INTERP_CC Value op_call_intrinsic1(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator = code_object->native_function_targets[target_idx].fixed1(
            thread, get_native_arg(fp, code_object, 0));
        COMPLETE();
    }

    static INTERP_CC Value op_call_intrinsic2(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator = code_object->native_function_targets[target_idx].fixed2(
            thread, get_native_arg(fp, code_object, 0),
            get_native_arg(fp, code_object, 1));
        COMPLETE();
    }

    static INTERP_CC Value op_call_intrinsic3(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator = code_object->native_function_targets[target_idx].fixed3(
            thread, get_native_arg(fp, code_object, 0),
            get_native_arg(fp, code_object, 1),
            get_native_arg(fp, code_object, 2));
        COMPLETE();
    }

    static INTERP_CC Value op_call_intrinsic4(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator = code_object->native_function_targets[target_idx].fixed4(
            thread, get_native_arg(fp, code_object, 0),
            get_native_arg(fp, code_object, 1),
            get_native_arg(fp, code_object, 2),
            get_native_arg(fp, code_object, 3));
        COMPLETE();
    }

    static INTERP_CC Value op_call_intrinsic5(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator = code_object->native_function_targets[target_idx].fixed5(
            thread, get_native_arg(fp, code_object, 0),
            get_native_arg(fp, code_object, 1),
            get_native_arg(fp, code_object, 2),
            get_native_arg(fp, code_object, 3),
            get_native_arg(fp, code_object, 4));
        COMPLETE();
    }

    static INTERP_CC Value op_call_intrinsic6(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator = code_object->native_function_targets[target_idx].fixed6(
            thread, get_native_arg(fp, code_object, 0),
            get_native_arg(fp, code_object, 1),
            get_native_arg(fp, code_object, 2),
            get_native_arg(fp, code_object, 3),
            get_native_arg(fp, code_object, 4),
            get_native_arg(fp, code_object, 5));
        COMPLETE();
    }

    static INTERP_CC Value op_call_intrinsic7(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator = code_object->native_function_targets[target_idx].fixed7(
            thread, get_native_arg(fp, code_object, 0),
            get_native_arg(fp, code_object, 1),
            get_native_arg(fp, code_object, 2),
            get_native_arg(fp, code_object, 3),
            get_native_arg(fp, code_object, 4),
            get_native_arg(fp, code_object, 5),
            get_native_arg(fp, code_object, 6));
        COMPLETE();
    }

    static INTERP_CC Value op_call_extension0(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        clover_context ctx{thread};
        accumulator = unwrap_clover_value(
            code_object->native_function_targets[target_idx].extension0(&ctx));
        COMPLETE();
    }

    static INTERP_CC Value op_call_extension1(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        clover_context ctx{thread};
        accumulator = unwrap_clover_value(
            code_object->native_function_targets[target_idx].extension1(
                &ctx, wrap_clover_value(get_native_arg(fp, code_object, 0))));
        COMPLETE();
    }

    static INTERP_CC Value op_call_extension2(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        clover_context ctx{thread};
        accumulator = unwrap_clover_value(
            code_object->native_function_targets[target_idx].extension2(
                &ctx, wrap_clover_value(get_native_arg(fp, code_object, 0)),
                wrap_clover_value(get_native_arg(fp, code_object, 1))));
        COMPLETE();
    }

    static INTERP_CC Value op_call_extension3(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        clover_context ctx{thread};
        accumulator = unwrap_clover_value(
            code_object->native_function_targets[target_idx].extension3(
                &ctx, wrap_clover_value(get_native_arg(fp, code_object, 0)),
                wrap_clover_value(get_native_arg(fp, code_object, 1)),
                wrap_clover_value(get_native_arg(fp, code_object, 2))));
        COMPLETE();
    }

    static INTERP_CC Value op_call_extension4(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        clover_context ctx{thread};
        accumulator = unwrap_clover_value(
            code_object->native_function_targets[target_idx].extension4(
                &ctx, wrap_clover_value(get_native_arg(fp, code_object, 0)),
                wrap_clover_value(get_native_arg(fp, code_object, 1)),
                wrap_clover_value(get_native_arg(fp, code_object, 2)),
                wrap_clover_value(get_native_arg(fp, code_object, 3))));
        COMPLETE();
    }

    static INTERP_CC Value op_call_extension5(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        clover_context ctx{thread};
        accumulator = unwrap_clover_value(
            code_object->native_function_targets[target_idx].extension5(
                &ctx, wrap_clover_value(get_native_arg(fp, code_object, 0)),
                wrap_clover_value(get_native_arg(fp, code_object, 1)),
                wrap_clover_value(get_native_arg(fp, code_object, 2)),
                wrap_clover_value(get_native_arg(fp, code_object, 3)),
                wrap_clover_value(get_native_arg(fp, code_object, 4))));
        COMPLETE();
    }

    static INTERP_CC Value op_call_extension6(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        clover_context ctx{thread};
        accumulator = unwrap_clover_value(
            code_object->native_function_targets[target_idx].extension6(
                &ctx, wrap_clover_value(get_native_arg(fp, code_object, 0)),
                wrap_clover_value(get_native_arg(fp, code_object, 1)),
                wrap_clover_value(get_native_arg(fp, code_object, 2)),
                wrap_clover_value(get_native_arg(fp, code_object, 3)),
                wrap_clover_value(get_native_arg(fp, code_object, 4)),
                wrap_clover_value(get_native_arg(fp, code_object, 5))));
        COMPLETE();
    }

    static INTERP_CC Value op_call_extension7(PARAMS)
    {
        START(2);
        uint8_t target_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        clover_context ctx{thread};
        accumulator = unwrap_clover_value(
            code_object->native_function_targets[target_idx].extension7(
                &ctx, wrap_clover_value(get_native_arg(fp, code_object, 0)),
                wrap_clover_value(get_native_arg(fp, code_object, 1)),
                wrap_clover_value(get_native_arg(fp, code_object, 2)),
                wrap_clover_value(get_native_arg(fp, code_object, 3)),
                wrap_clover_value(get_native_arg(fp, code_object, 4)),
                wrap_clover_value(get_native_arg(fp, code_object, 5)),
                wrap_clover_value(get_native_arg(fp, code_object, 6))));
        COMPLETE();
    }

    static INTERP_CC Value op_call_runtime_intrinsic0(PARAMS)
    {
        START(2);
        RuntimeIntrinsic0 intrinsic = RuntimeIntrinsic0(pc[1]);
        switch(intrinsic)
        {
            case RuntimeIntrinsic0::Globals:
                {
                    CodeObject *caller_code_object =
                        fp[FrameHeaderReturnCodeObjectOffset]
                            .get_ptr<CodeObject>();
                    ModuleObject *caller_module =
                        caller_code_object->get_defining_module().extract();
                    accumulator =
                        thread->make_object_value<SlotDict>(caller_module)
                            .raw_value();
                    COMPLETE();
                }
            case RuntimeIntrinsic0::Locals:
                {
                    CodeObject *caller_code_object =
                        fp[FrameHeaderReturnCodeObjectOffset]
                            .get_ptr<CodeObject>();
                    if(caller_code_object->local_scope != nullptr)
                    {
                        accumulator =
                            thread->set_pending_builtin_exception_string(
                                L"UnimplementedError",
                                L"locals() is only implemented for module "
                                L"scope");
                        COMPLETE();
                    }
                    ModuleObject *caller_module =
                        caller_code_object->get_defining_module().extract();
                    accumulator =
                        thread->make_object_value<SlotDict>(caller_module)
                            .raw_value();
                    COMPLETE();
                }
            case RuntimeIntrinsic0::ImportStar:
                {
                    thread->set_clover_frame_frontier(fp);
                    accumulator = import_star(thread, code_object, accumulator);
                    if(unlikely(accumulator.is_exception_marker()))
                    {
                        MUSTTAIL return propagate_pending_exception(ARGS);
                    }
                    COMPLETE();
                }
        }
        __builtin_unreachable();
    }

    static INTERP_CC Value op_import_name(PARAMS)
    {
        START(3);
        uint8_t name_idx = pc[1];
        uint8_t level = pc[2];
        Value fromlist = accumulator;
        thread->set_clover_frame_frontier(fp);
        accumulator =
            import_name_from_code(thread, code_object,
                                  TValue<String>::from_value_assumed(
                                      code_object->constant_table[name_idx]),
                                  fromlist, level);
        if(unlikely(accumulator.is_exception_marker()))
        {
            MUSTTAIL return propagate_pending_exception(ARGS);
        }
        COMPLETE();
    }

    static INTERP_CC Value op_import_from(PARAMS)
    {
        START(2);
        uint8_t name_idx = pc[1];
        thread->set_clover_frame_frontier(fp);
        accumulator = import_from(thread, accumulator,
                                  TValue<String>::from_value_assumed(
                                      code_object->constant_table[name_idx]));
        if(unlikely(accumulator.is_exception_marker()))
        {
            MUSTTAIL return propagate_pending_exception(ARGS);
        }
        COMPLETE();
    }

    static INTERP_CC Value op_for_iter(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value iterator_value = fp[reg];

        if(unlikely(!iterator_value.is_ptr()))
        {
            MUSTTAIL return not_iterator_error(ARGS);
        }

        Object *iterator_object = iterator_value.get_ptr();
        if(unlikely(iterator_object->native_layout_id() !=
                    NativeLayoutId::RangeIterator))
        {
            MUSTTAIL return not_iterator_error(ARGS);
        }

        RangeIterator *iterator = static_cast<RangeIterator *>(iterator_object);
        Value current = iterator->current.raw_value();
        Value stop = iterator->stop.raw_value();
        Value step = iterator->step.raw_value();

        if(unlikely(!current.is_smi() || !stop.is_smi() || !step.is_smi()))
        {
            MUSTTAIL return invalid_range_iteration_state_error(ARGS);
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        int64_t step_smi = step.get_smi();

        if(unlikely(step_smi == 0))
        {
            MUSTTAIL return invalid_range_iteration_state_error(ARGS);
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
            iterator->current = TValue<SMI>::from_smi(next_smi);
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_for_prep_range1(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable != thread->get_machine()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value stop = fp[reg - 1];
            if(unlikely(!stop.is_integer()))
            {
                MUSTTAIL return range_integer_argument_error(ARGS);
            }
            fp[reg] = Value::from_smi(0);
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_for_prep_range2(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable != thread->get_machine()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value start = fp[reg - 1];
            Value stop = fp[reg - 2];
            if(unlikely(!start.is_integer() || !stop.is_integer()))
            {
                MUSTTAIL return range_integer_argument_error(ARGS);
            }
            fp[reg] = start;
            fp[reg - 1] = stop;
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_for_prep_range3(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value callable = fp[reg];
        pc += 4;
        if(callable != thread->get_machine()->get_range_builtin())
        {
            pc += rel_target;
        }
        else
        {
            Value start = fp[reg - 1];
            Value stop = fp[reg - 2];
            Value step = fp[reg - 3];
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
            fp[reg - 1] = stop;
            fp[reg - 2] = step;
        }

        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_for_iter_range1(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value current = fp[reg];
        Value stop = fp[reg - 1];

        if(unlikely(!current.is_smi() || !stop.is_smi()))
        {
            MUSTTAIL return invalid_range_iteration_state_error(ARGS);
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

    static INTERP_CC Value op_for_iter_range_step(PARAMS)
    {
        int8_t reg = pc[1];
        int16_t rel_target = read_int16_le(&pc[2]);
        Value current = fp[reg];
        Value stop = fp[reg - 1];
        Value step = fp[reg - 2];

        if(unlikely(!current.is_smi() || !stop.is_smi() || !step.is_smi()))
        {
            MUSTTAIL return invalid_range_iteration_state_error(ARGS);
        }

        int64_t current_smi = current.get_smi();
        int64_t stop_smi = stop.get_smi();
        int64_t step_smi = step.get_smi();
        if(unlikely(step_smi == 0))
        {
            MUSTTAIL return invalid_range_iteration_state_error(ARGS);
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

    static INTERP_CC Value op_return(PARAMS)
    {
        restore_frame_header(fp, pc, code_object);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_with_accumulator_slow(ARGS);
        }

        START(0);
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value op_return_or_raise_exception_slow(PARAMS)
    {
        if(!thread->has_pending_exception())
        {
            MUSTTAIL return exception_marker_without_pending_exception_system_error(
                ARGS);
        }

        if(thread->pending_exception_kind() ==
           PendingExceptionKind::StopIteration)
        {
            materialize_pending_stop_iteration(thread);
        }
        else if(thread->pending_exception_kind() !=
                PendingExceptionKind::Object)
        {
            MUSTTAIL return exception_marker_without_pending_exception_system_error(
                ARGS);
        }

        ExceptionalTarget target =
            resolve_exceptional_frame_exit(thread, fp, pc, code_object);
        fp = target.fp;
        code_object = target.code_object;
        pc = target.interpreted_pc;
        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_return_or_raise_exception(PARAMS)
    {
        if(unlikely(accumulator.is_exception_marker()))
        {
            MUSTTAIL return op_return_or_raise_exception_slow(ARGS);
        }

        restore_frame_header(fp, pc, code_object);
        if(unlikely(thread->safepoint_requested()))
        {
            MUSTTAIL return op_committed_safepoint_with_accumulator_slow(ARGS);
        }
        START(0);
        COMPLETE();
    }

    static INTERP_CC Value op_return_to_native(PARAMS)
    {
        START(1);
        Value *restored_fp = (Value *)fp[FrameHeaderPreviousFpOffset].as.ptr;
        thread->set_clover_frame_frontier(restored_fp);
        return accumulator;
        COMPLETE();
    }

    NOINLINE static INTERP_CC Value
    op_return_exception_marker_to_native_slow(PARAMS)
    {
        MUSTTAIL return exception_marker_native_return_without_pending_exception_system_error(
            ARGS);
    }

    static INTERP_CC Value op_return_exception_marker_to_native(PARAMS)
    {
        START(1);
        if(unlikely(!thread->has_pending_exception()))
        {
            MUSTTAIL return op_return_exception_marker_to_native_slow(ARGS);
        }

        Value *restored_fp = (Value *)fp[FrameHeaderPreviousFpOffset].as.ptr;
        thread->set_clover_frame_frontier(restored_fp);
        return Value::exception_marker();
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
        SET_TABLE_ENTRY(Bytecode::LoadLocalChecked, op_load_local_checked);
        SET_TABLE_ENTRY(Bytecode::ClearLocal, op_clear_local);
        SET_TABLE_ENTRY(Bytecode::Star, op_star);
        SET_TABLE_ENTRY(Bytecode::Mov, op_mov);
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
        SET_TABLE_ENTRY(Bytecode::TrueDiv, op_truediv);
        SET_TABLE_ENTRY(Bytecode::FloorDiv, op_floordiv);
        SET_TABLE_ENTRY(Bytecode::FloorDivSmi, op_floordiv_smi);
        SET_TABLE_ENTRY(Bytecode::Mod, op_mod);
        SET_TABLE_ENTRY(Bytecode::ModSmi, op_mod_smi);
        SET_TABLE_ENTRY(Bytecode::LShift, op_lshift);
        SET_TABLE_ENTRY(Bytecode::LShiftSmi, op_lshift_smi);
        SET_TABLE_ENTRY(Bytecode::RShift, op_rshift);
        SET_TABLE_ENTRY(Bytecode::RShiftSmi, op_rshift_smi);

        SET_TABLE_ENTRY(Bytecode::TestIs, op_is);
        SET_TABLE_ENTRY(Bytecode::TestIsNot, op_is_not);
        SET_TABLE_ENTRY(Bytecode::TestEqual, op_eq);
        SET_TABLE_ENTRY(Bytecode::TestNotEqual, op_ne);
        SET_TABLE_ENTRY(Bytecode::TestLess, op_lt);
        SET_TABLE_ENTRY(Bytecode::TestLessEqual, op_le);
        SET_TABLE_ENTRY(Bytecode::TestGreaterEqual, op_ge);
        SET_TABLE_ENTRY(Bytecode::TestGreater, op_gt);
        SET_TABLE_ENTRY(Bytecode::CheckOperatorNotImplemented,
                        op_check_operator_not_implemented);

        SET_TABLE_ENTRY(Bytecode::LdaGlobal, op_lda_global);
        SET_TABLE_ENTRY(Bytecode::StaGlobal, op_sta_global);
        SET_TABLE_ENTRY(Bytecode::DelGlobal, op_del_global);
        SET_TABLE_ENTRY(Bytecode::DelLocal, op_del_local);
        SET_TABLE_ENTRY(Bytecode::LoadAttr, op_load_attr);
        SET_TABLE_ENTRY(Bytecode::StoreAttr, op_store_attr);
        SET_TABLE_ENTRY(Bytecode::DelAttr, op_del_attr);
        SET_TABLE_ENTRY(Bytecode::GetItem, op_get_item);
        SET_TABLE_ENTRY(Bytecode::SetItem, op_set_item);
        SET_TABLE_ENTRY(Bytecode::DelItem, op_del_item);
        SET_TABLE_ENTRY(Bytecode::CallMethodAttrPositional,
                        op_call_method_attr_positional);
        SET_TABLE_ENTRY(Bytecode::CallMethodAttrKeyword,
                        op_call_method_attr_keyword);
        SET_TABLE_ENTRY(Bytecode::CallSpecialMethod, op_call_special_method);

        SET_TABLE_ENTRY(Bytecode::Neg, op_negate);
        SET_TABLE_ENTRY(Bytecode::Pos, op_plus);
        SET_TABLE_ENTRY(Bytecode::Sqrt, op_sqrt);
        SET_TABLE_ENTRY(Bytecode::Not, op_not);

        SET_TABLE_ENTRY(Bytecode::CreateDict, op_create_dict);
        SET_TABLE_ENTRY(Bytecode::CreateList, op_create_list);
        SET_TABLE_ENTRY(Bytecode::CreateTuple, op_create_tuple);
        SET_TABLE_ENTRY(Bytecode::CreateBinarySlice, op_create_binary_slice);
        SET_TABLE_ENTRY(Bytecode::CreateTernarySlice, op_create_ternary_slice);
        SET_TABLE_ENTRY(Bytecode::CreateFunction, op_create_function);
        SET_TABLE_ENTRY(Bytecode::CreateFunctionWithDefaults,
                        op_create_function_with_defaults);
        SET_TABLE_ENTRY(Bytecode::CreateInstanceKnownClass,
                        op_create_instance_known_class);
        SET_TABLE_ENTRY(Bytecode::IsInstanceOfKnownClass,
                        op_is_instance_of_known_class);
        SET_TABLE_ENTRY(Bytecode::CreateClass, op_create_class);
        SET_TABLE_ENTRY(Bytecode::BuildClass, op_build_class);
        SET_TABLE_ENTRY(Bytecode::CheckInitReturnedNone,
                        op_check_init_returned_none);
        SET_TABLE_ENTRY(Bytecode::WriteStdout, op_write_stdout);
        SET_TABLE_ENTRY(Bytecode::RaiseAssertionError,
                        op_raise_assertion_error);
        SET_TABLE_ENTRY(Bytecode::RaiseAssertionErrorWithMessage,
                        op_raise_assertion_error_with_message);
        SET_TABLE_ENTRY(Bytecode::RaiseUnwind, op_raise_unwind);
        SET_TABLE_ENTRY(Bytecode::RaiseUnwindWithContext,
                        op_raise_unwind_with_context);
        SET_TABLE_ENTRY(Bytecode::RaiseBare, op_raise_bare);

        SET_TABLE_ENTRY(Bytecode::CallPositional, op_call_positional);
        SET_TABLE_ENTRY(Bytecode::CallKeyword, op_call_keyword);
        SET_TABLE_ENTRY(Bytecode::CallIntrinsic0, op_call_intrinsic0);
        SET_TABLE_ENTRY(Bytecode::CallIntrinsic1, op_call_intrinsic1);
        SET_TABLE_ENTRY(Bytecode::CallIntrinsic2, op_call_intrinsic2);
        SET_TABLE_ENTRY(Bytecode::CallIntrinsic3, op_call_intrinsic3);
        SET_TABLE_ENTRY(Bytecode::CallIntrinsic4, op_call_intrinsic4);
        SET_TABLE_ENTRY(Bytecode::CallIntrinsic5, op_call_intrinsic5);
        SET_TABLE_ENTRY(Bytecode::CallIntrinsic6, op_call_intrinsic6);
        SET_TABLE_ENTRY(Bytecode::CallIntrinsic7, op_call_intrinsic7);
        SET_TABLE_ENTRY(Bytecode::CallExtension0, op_call_extension0);
        SET_TABLE_ENTRY(Bytecode::CallExtension1, op_call_extension1);
        SET_TABLE_ENTRY(Bytecode::CallExtension2, op_call_extension2);
        SET_TABLE_ENTRY(Bytecode::CallExtension3, op_call_extension3);
        SET_TABLE_ENTRY(Bytecode::CallExtension4, op_call_extension4);
        SET_TABLE_ENTRY(Bytecode::CallExtension5, op_call_extension5);
        SET_TABLE_ENTRY(Bytecode::CallExtension6, op_call_extension6);
        SET_TABLE_ENTRY(Bytecode::CallExtension7, op_call_extension7);
        SET_TABLE_ENTRY(Bytecode::CallRuntimeIntrinsic0,
                        op_call_runtime_intrinsic0);
        SET_TABLE_ENTRY(Bytecode::CallCodeObject, op_call_code_object);
        SET_TABLE_ENTRY(Bytecode::ImportName, op_import_name);
        SET_TABLE_ENTRY(Bytecode::ImportFrom, op_import_from);
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
        SET_TABLE_ENTRY(Bytecode::ReturnOrRaiseException,
                        op_return_or_raise_exception);
        SET_TABLE_ENTRY(Bytecode::ReturnToNative, op_return_to_native);
        SET_TABLE_ENTRY(Bytecode::ReturnExceptionMarkerToNative,
                        op_return_exception_marker_to_native);
        SET_TABLE_ENTRY(Bytecode::LdaActiveException, op_lda_active_exception);
        SET_TABLE_ENTRY(Bytecode::ActiveExceptionIsInstance,
                        op_active_exception_is_instance);
        SET_TABLE_ENTRY(Bytecode::DrainActiveExceptionInto,
                        op_drain_active_exception_into);
        SET_TABLE_ENTRY(Bytecode::ClearActiveException,
                        op_clear_active_exception);
        SET_TABLE_ENTRY(Bytecode::ReraiseActiveException,
                        op_reraise_active_exception);

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

    enum class TraceFrameTransition
    {
        Enter,
        Resume,
        Switch,
    };

    Value *trace_previous_frame(Value *fp)
    {
        return reinterpret_cast<Value *>(
            fp[FrameHeaderPreviousFpOffset].as.ptr);
    }

    TraceFrameTransition classify_trace_frame_transition(Value *previous_fp,
                                                         Value *current_fp)
    {
        if(previous_fp == nullptr ||
           trace_previous_frame(current_fp) == previous_fp)
        {
            return TraceFrameTransition::Enter;
        }
        if(trace_previous_frame(previous_fp) == current_fp)
        {
            return TraceFrameTransition::Resume;
        }
        return TraceFrameTransition::Switch;
    }

    template <typename Out>
    Out format_code_object_trace_name(Out out, CodeObject *code_object)
    {
        return format_string_contents(out, code_object->name.extract());
    }

    NOINLINE void trace_frame_transition(ThreadState *thread, Value *fp,
                                         CodeObject *code_object)
    {
        Value *previous_fp = thread->trace_interpreter_frame();
        if(previous_fp == fp)
        {
            return;
        }

        TraceFrameTransition transition =
            classify_trace_frame_transition(previous_fp, fp);
        thread->set_trace_interpreter_frame(fp);

        fmt::memory_buffer buffer;
        auto out = std::back_inserter(buffer);
        switch(transition)
        {
            case TraceFrameTransition::Enter:
                out = fmt::format_to(out, ">>> enter ");
                break;
            case TraceFrameTransition::Resume:
                out = fmt::format_to(out, "<<< resume ");
                break;
            case TraceFrameTransition::Switch:
                out = fmt::format_to(out, ">>> frame ");
                break;
        }
        out = format_code_object_trace_name(out, code_object);
        fmt::format_to(out, "\n");
        fmt::print(stderr, "{}", fmt::to_string(buffer));
    }

    NOINLINE void trace_current_instruction(CodeObject *code_object,
                                            const uint8_t *pc)
    {
        fmt::memory_buffer buffer;
        auto out = std::back_inserter(buffer);
        fmt::formatter<CodeObject> formatter;
        formatter.disassemble_instruction(
            *code_object, out, code_object->offset_for_interpreted_pc(pc));
        fmt::print(stderr, "{}", fmt::to_string(buffer));
    }

    static INTERP_CC Value trace_instruction(PARAMS)
    {
        trace_frame_transition(thread, fp, code_object);
        trace_current_instruction(code_object, pc);
        auto *dispatch_fun = dispatch_table.table[*pc];
        MUSTTAIL return dispatch_fun(ARGS);
    }

    DispatchTable make_trace_dispatch_table()
    {
        DispatchTable tbl;
        for(size_t i = 0; i < BytecodeTableSize; ++i)
        {
            tbl.table[i] = trace_instruction;
        }
        return tbl;
    }

    DispatchTable trace_dispatch_table = make_trace_dispatch_table();

    Value run_interpreter(Value *fp, CodeObject *code_object, uint32_t start_pc,
                          ThreadState *thread)
    {
        assert(is_stack_frame_aligned(fp));
        const uint8_t *pc = &code_object->code[start_pc];
        DispatchTable *active_dispatch_table =
            thread->trace_interpreter_instructions() ? &trace_dispatch_table
                                                     : &dispatch_table;
        void *dispatch = reinterpret_cast<void *>(active_dispatch_table);
        Value accumulator = Value::from_smi(0);  // init accumulator to 0

        // do the initial dispatch
        auto *dispatch_fun = active_dispatch_table->table[*pc];
        return dispatch_fun(ARGS);
    }

}  // namespace cl
