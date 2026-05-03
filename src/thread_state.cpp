#include "thread_state.h"
#include "class_object.h"
#include "codegen.h"
#include "compilation_unit.h"
#include "exception_object.h"
#include "interpreter.h"
#include "owned_typed_value.h"
#include "parser.h"
#include "runtime_helpers.h"
#include "startup_wrapper.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <stdexcept>
#include <string>

namespace cl
{

    thread_local ThreadState *ThreadState::current_thread = nullptr;

    PendingException::PendingException()
        : stop_iteration_value(Value::not_present())
    {
    }

    ThreadState::ThreadState(VirtualMachine *_machine)
        : machine(_machine),
          refcounted_heap(&machine->get_refcounted_global_heap()),
          stack(1024 * 1024)
    {
    }

    Value ThreadState::run(CodeObject *obj)
    {
        ActivationScope activation_scope(this);
        OwnedTValue<CodeObject> startup_wrapper(
            make_startup_wrapper_code_object(obj));
        return run_interpreter(&stack[stack.size() - 1024],
                               startup_wrapper.extract(), 0);
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
            make_exception_object(type, message));
    }

    Value ThreadState::set_pending_exception_string(TValue<ClassObject> type,
                                                    const wchar_t *message)
    {
        return set_pending_exception_string(type, interned_string(message));
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
        ts->zero_count_table.push_back(obj);
    }

}  // namespace cl
