#include "thread_state.h"
#include "codegen.h"
#include "compilation_unit.h"
#include "interpreter.h"
#include "parser.h"
#include "runtime_helpers.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <stdexcept>

namespace cl
{

    thread_local ThreadState *ThreadState::current_thread = nullptr;

    PendingException::PendingException()
        : object(Value::None()), stop_iteration_value(Value::not_present())
    {
    }

    ThreadState::ThreadState(VirtualMachine *_machine)
        : machine(_machine),
          refcounted_heap(&machine->get_refcounted_global_heap()),
          stack(1024 * 1024)
    {
    }

    ThreadState::~ThreadState()
    {
        ActivationScope activation_scope(this);
        clear_pending_exception();
    }

    Value ThreadState::run(CodeObject *obj)
    {
        ActivationScope activation_scope(this);
        return run_interpreter(&stack[stack.size() - 1024], obj, 0);
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
        pending_exception.object = Value::None();
        pending_exception.stop_iteration_value = Value::not_present();
    }

    void ThreadState::set_pending_exception_object(Value exception_object)
    {
        pending_exception.object = exception_object;
        pending_exception.stop_iteration_value = Value::not_present();
        pending_exception.kind = PendingExceptionKind::Object;
    }

    void ThreadState::set_pending_exception_string(TValue<ClassObject>,
                                                   const char *)
    {
        throw std::runtime_error(
            "set_pending_exception_string requires exception object support");
    }

    void ThreadState::set_pending_exception_none(TValue<ClassObject> type)
    {
        set_pending_exception_string(type, "");
    }

    void ThreadState::set_pending_stop_iteration_no_value()
    {
        pending_exception.object = Value::None();
        pending_exception.stop_iteration_value = Value::not_present();
        pending_exception.kind = PendingExceptionKind::StopIteration;
    }

    void ThreadState::set_pending_stop_iteration_value(Value value)
    {
        pending_exception.object = Value::None();
        pending_exception.stop_iteration_value = value;
        pending_exception.kind = PendingExceptionKind::StopIteration;
    }

    Value ThreadState::pending_exception_object() const
    {
        assert(pending_exception.kind == PendingExceptionKind::Object);
        return pending_exception.object;
    }

    Value ThreadState::pending_stop_iteration_value() const
    {
        assert(pending_exception.kind == PendingExceptionKind::StopIteration);
        return pending_exception.stop_iteration_value;
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
