#include "thread_state.h"
#include "class_object.h"
#include "clover_entry.h"
#include "code_object.h"
#include "codegen.h"
#include "compilation_unit.h"
#include "exception_object.h"
#include "function.h"
#include "interpreter.h"
#include "owned_typed_value.h"
#include "parser.h"
#include "runtime_helpers.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace cl
{
    static_assert(sizeof(Value) == 8);
    static_assert(FrameHeaderSizeAboveFp * sizeof(Value) == 32);
    static_assert((FrameHeaderSizeAboveFp * sizeof(Value)) %
                      FrameAlignmentBytes ==
                  0);

    thread_local ThreadState *ThreadState::current_thread = nullptr;

    PendingException::PendingException()
        : stop_iteration_value(Value::not_present())
    {
    }

    static Value *align_clover_frame_pointer_down(Value *fp)
    {
        uintptr_t address = reinterpret_cast<uintptr_t>(fp);
        address &= ~(FrameAlignmentBytes - 1);
        return reinterpret_cast<Value *>(address);
    }

    static Value *initial_clover_frame_frontier(std::vector<Value> &stack)
    {
        Value *highest_fp_with_header =
            stack.data() + stack.size() - FrameHeaderSizeAboveFp;
        return align_clover_frame_pointer_down(highest_fp_with_header);
    }

    ThreadState::ThreadState(VirtualMachine *_machine)
        : machine(_machine),
          refcounted_heap(&machine->get_refcounted_global_heap()),
          stack(1024 * 1024)
    {
        set_clover_frame_frontier(initial_clover_frame_frontier(stack));
    }

    static void initialize_initial_clover_frame(Value *initial_fp)
    {
        initial_fp[FrameHeaderPreviousFpOffset].as.ptr = nullptr;
        initial_fp[FrameHeaderCompiledReturnPcOffset].as.ptr = nullptr;
        initial_fp[FrameHeaderReturnCodeObjectOffset].as.ptr = nullptr;
        initial_fp[FrameHeaderReturnPcOffset].as.ptr = nullptr;
    }

    Value ThreadState::run(CodeObject *obj)
    {
        ActivationScope activation_scope(this);
        initialize_initial_clover_frame(clover_frame_frontier());
        OwnedTValue<CodeObject> startup_wrapper(
            make_startup_wrapper_code_object(obj));
        return run_interpreter(clover_frame_frontier(),
                               startup_wrapper.extract(), 0, this);
    }

    static void set_clover_entry_adapter_parameter(CodeObject *adapter,
                                                   Value *adapter_fp,
                                                   uint32_t parameter_idx,
                                                   Value value)
    {
        adapter_fp[adapter->encode_reg(parameter_idx)] = value;
    }

    Value ThreadState::call_clovervm_function_with_args(
        TValue<Function> function, const Value *args, uint32_t n_args)
    {
        ActivationScope activation_scope(this);
        CodeObject *adapter = machine->clover_function_entry_adapter(n_args);
        Value *caller_fp = clover_frame_frontier();
        Value *adapter_fp =
            caller_fp - adapter->get_highest_occupied_frame_offset() - 1;
        adapter_fp[FrameHeaderPreviousFpOffset].as.ptr =
            reinterpret_cast<Object *>(caller_fp);

        set_clover_entry_adapter_parameter(adapter, adapter_fp, 0,
                                           function.as_value());
        for(uint32_t arg_idx = 0; arg_idx < n_args; ++arg_idx)
        {
            set_clover_entry_adapter_parameter(adapter, adapter_fp, arg_idx + 1,
                                               args[arg_idx]);
        }

        set_clover_frame_frontier(adapter_fp);
        return run_interpreter(adapter_fp, adapter, 0, this);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function)
    {
        return call_clovervm_function_with_args(function, nullptr, 0);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0)
    {
        Value args[] = {arg0};
        return call_clovervm_function_with_args(function, args, 1);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0, Value arg1)
    {
        Value args[] = {arg0, arg1};
        return call_clovervm_function_with_args(function, args, 2);
    }

    Value ThreadState::call_clovervm_function(TValue<Function> function,
                                              Value arg0, Value arg1,
                                              Value arg2)
    {
        Value args[] = {arg0, arg1, arg2};
        return call_clovervm_function_with_args(function, args, 3);
    }

    bool ThreadState::has_pending_exception() const
    {
        return pending_exception.kind != PendingExceptionKind::None;
    }

    PendingExceptionKind ThreadState::pending_exception_kind() const
    {
        return pending_exception.kind;
    }

    void ThreadState::clear_pending_exception()
    {
        pending_exception.kind = PendingExceptionKind::None;
        pending_exception.object.clear();
        pending_exception.stop_iteration_value = Value::not_present();
    }

    Value
    ThreadState::set_pending_exception_object(TValue<ExceptionObject> exception)
    {
        pending_exception.object = exception;
        pending_exception.stop_iteration_value = Value::not_present();
        pending_exception.kind = PendingExceptionKind::Object;
        return Value::exception_marker();
    }

    Value ThreadState::set_pending_exception_string(TValue<ClassObject> type,
                                                    TValue<String> message)
    {
        return set_pending_exception_object(
            make_internal_value<ExceptionObject>(type.extract(), message));
    }

    Value ThreadState::set_pending_exception_string(TValue<ClassObject> type,
                                                    const wchar_t *message)
    {
        return set_pending_exception_string(
            type, machine->get_or_create_interned_string_value(message));
    }

    Value ThreadState::set_pending_exception_none(TValue<ClassObject> type)
    {
        return set_pending_exception_string(type, L"");
    }

    Value
    ThreadState::set_pending_builtin_exception_string(const wchar_t *type_name,
                                                      TValue<String> message)
    {
        return set_pending_exception_string(
            TValue<ClassObject>::from_oop(class_for_builtin_name(type_name)),
            message);
    }

    Value
    ThreadState::set_pending_builtin_exception_string(const wchar_t *type_name,
                                                      const wchar_t *message)
    {
        return set_pending_builtin_exception_string(type_name,
                                                    interned_string(message));
    }

    Value
    ThreadState::set_pending_builtin_exception_none(const wchar_t *type_name)
    {
        return set_pending_builtin_exception_string(type_name, L"");
    }

    Value ThreadState::set_pending_stop_iteration_no_value()
    {
        pending_exception.object.clear();
        pending_exception.stop_iteration_value = Value::not_present();
        pending_exception.kind = PendingExceptionKind::StopIteration;
        return Value::exception_marker();
    }

    Value ThreadState::set_pending_stop_iteration_value(Value value)
    {
        pending_exception.object.clear();
        pending_exception.stop_iteration_value = value;
        pending_exception.kind = PendingExceptionKind::StopIteration;
        return Value::exception_marker();
    }

    Value ThreadState::pending_exception_object() const
    {
        assert(pending_exception.kind == PendingExceptionKind::Object);
        return pending_exception.object.as_value();
    }

    Value ThreadState::pending_stop_iteration_value() const
    {
        assert(pending_exception.kind == PendingExceptionKind::StopIteration);
        return pending_exception.stop_iteration_value;
    }

    ClassObject *ThreadState::class_for_builtin_name(const wchar_t *name) const
    {
        TValue<String> name_value =
            machine->get_or_create_interned_string_value(std::wstring(name));
        Value value =
            machine->get_builtin_scope().extract()->get_by_name(name_value);
        assert(value.is_ptr());
        assert(value.get_ptr<Object>()->native_layout_id() ==
               NativeLayoutId::ClassObject);
        return value.get_ptr<ClassObject>();
    }

    CodeObject *ThreadState::compile(const wchar_t *str, StartRule start_rule)
    {
        return compile(str, start_rule, L"<module>");
    }

    CodeObject *ThreadState::compile(const wchar_t *str, StartRule start_rule,
                                     const wchar_t *module_name)
    {
        ActivationScope activation_scope(this);

        CompilationUnit input(str);
        TokenVector tv = tokenize(input);
        AstVector av = parse(*machine, tv, start_rule);
        return codegen_module(av, interned_string(module_name));
    }

    void ThreadState::add_to_active_zero_count_table(HeapObject *obj)
    {
        ThreadState *ts = ThreadState::get_active();
        ts->add_to_zero_count_table(obj);
    }

}  // namespace cl
