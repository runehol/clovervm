#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <vector>
#include <memory>
#include <deque>

#include "value.h"
#include "heap.h"
#include "code_object.h"

namespace cl
{
    enum class StartRule;

    class VirtualMachine;
    struct CodeObject;

    class ThreadState
    {
    public:
        ThreadState(VirtualMachine *_machine);
        ~ThreadState();

        static void add_to_active_zero_count_table(Value v);


        Value run(CodeObject *obj);

        static ThreadState *get_active()
        {
            assert(current_thread != nullptr);
            return current_thread;
        }


        void *allocate_refcounted(size_t n_bytes) { return refcounted_heap.allocate(n_bytes); }

        CodeObject compile(const wchar_t *str, StartRule start_rule);

    private:
        VirtualMachine *machine;

        ThreadLocalHeap refcounted_heap;

        std::vector<Value> stack;
        std::deque<Value> zero_count_table;




        static thread_local ThreadState *current_thread;

        class CurrThreadStateHolder
        {
        public:
            CurrThreadStateHolder(ThreadState *ts)
            {
                ThreadState::current_thread = ts;
            }
            ~CurrThreadStateHolder()
            {
                ThreadState::current_thread = nullptr;
            }
        };

    };



}

#endif //CL_THREAD_STATE_H
