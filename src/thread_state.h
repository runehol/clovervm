#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <deque>
#include <memory>
#include <vector>

#include "heap.h"
#include "value.h"
#include <type_traits>
#include <utility>

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

        static ThreadState *get_active_if_any() { return current_thread; }

        void *allocate_refcounted(size_t n_bytes)
        {
            return refcounted_heap.allocate(n_bytes);
        }

        template <typename T, typename... Args>
        T *make_refcounted(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            return refcounted_heap.make<T>(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        T *make_refcounted_sized(size_t n_bytes, Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            return refcounted_heap.make_sized<T>(n_bytes,
                                                 std::forward<Args>(args)...);
        }

        CodeObject *compile(const wchar_t *str, StartRule start_rule);

        VirtualMachine *get_machine() const { return machine; }

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
            ~CurrThreadStateHolder() { ThreadState::current_thread = nullptr; }
        };
    };

}  // namespace cl

#endif  // CL_THREAD_STATE_H
