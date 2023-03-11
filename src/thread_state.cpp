#include "thread_state.h"
#include "virtual_machine.h"
#include "interpreter.h"
#include "tokenizer.h"
#include "compilation_unit.h"
#include "parser.h"
#include "codegen.h"

namespace cl
{

    thread_local ThreadState *ThreadState::current_thread = nullptr;


    ThreadState::ThreadState(VirtualMachine *_machine)
        : machine(_machine),
          refcounted_heap(&machine->get_refcounted_global_heap()),
          stack(1024*1024)
    {

    }

    ThreadState::~ThreadState() = default;


    Value ThreadState::run(CodeObject *obj)
    {
        CurrThreadStateHolder curr_thread_holder(this);
        return run_interpreter(&stack[stack.size()-1024], obj, 0);
    }

    CodeObject ThreadState::compile(const wchar_t *str, StartRule start_rule)
    {
        CurrThreadStateHolder curr_thread_holder(this);

        CompilationUnit input(str);
        TokenVector tv = tokenize(input);
        AstVector av = parse(*machine, tv, start_rule);
        return generate_code(av);
     }


    void ThreadState::add_to_active_zero_count_table(Value v)
    {
        ThreadState *ts = ThreadState::get_active();
        ts->zero_count_table.push_back(v);
    }



}
