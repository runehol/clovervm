#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <deque>
#include <memory>
#include <vector>

#include "heap.h"
#include "owned.h"
#include "typed_value.h"
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
        class ActivationScope
        {
        public:
            explicit ActivationScope(ThreadState *ts)
                : previous_thread(ThreadState::current_thread)
            {
                ThreadState::current_thread = ts;
            }

            ~ActivationScope()
            {
                ThreadState::current_thread = previous_thread;
            }

        private:
            ThreadState *previous_thread;
        };

        ThreadState(VirtualMachine *_machine);
        ~ThreadState();

        static void add_to_active_zero_count_table(Value v);

        Value run(CodeObject *obj);

        static ThreadState *get_active()
        {
            assert(current_thread != nullptr);
            return current_thread;
        }

        template <typename T, typename... Args>
        T *make_refcounted_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            static_assert(HasObjectLayout<T>::value);
            return refcounted_heap.make<T>(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        TValue<T> make_refcounted_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_refcounted_raw<T>(std::forward<Args>(args)...));
        }

        CodeObject *compile(const wchar_t *str, StartRule start_rule);

        VirtualMachine *get_machine() const { return machine; }
        void push_pending_class_definition_name(Value class_name);
        Value pop_pending_class_definition_name();

    private:
        VirtualMachine *machine;

        ThreadLocalHeap refcounted_heap;

        std::vector<Value> stack;
        std::deque<Value> zero_count_table;
        std::vector<OwnedValue> pending_class_definition_names;

        static thread_local ThreadState *current_thread;
    };

}  // namespace cl

#endif  // CL_THREAD_STATE_H
