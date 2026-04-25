#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <deque>
#include <memory>
#include <vector>

#include "heap.h"
#include "typed_value.h"
#include "value.h"
#include <type_traits>
#include <utility>

namespace cl
{
    enum class StartRule;

    class ClassObject;
    class VirtualMachine;
    struct CodeObject;

    ClassObject *class_for_native_layout(VirtualMachine *vm, NativeLayoutId id);

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

        static void add_to_active_zero_count_table(HeapObject *obj);

        Value run(CodeObject *obj);

        static ThreadState *get_active()
        {
            assert(current_thread != nullptr);
            return current_thread;
        }

        template <typename T, typename... Args>
        T *make_refcounted_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<HeapObject, T>);
            static_assert(HasObjectLayout<T>::value);
            return refcounted_heap.make<T>(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        TValue<T> make_refcounted_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_refcounted_raw<T>(std::forward<Args>(args)...));
        }

        ClassObject *class_for_native_layout(NativeLayoutId id) const
        {
            return cl::class_for_native_layout(machine, id);
        }

        template <typename T, typename... Args>
        T *make_refcounted_object_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            static_assert(HasNativeLayoutId<T>::value);
            ClassObject *cls = class_for_native_layout(T::native_layout_id);
            assert(cls != nullptr);
            return make_refcounted_raw<T>(cls, std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        TValue<T> make_refcounted_object_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_refcounted_object_raw<T>(std::forward<Args>(args)...));
        }

        CodeObject *compile(const wchar_t *str, StartRule start_rule);

        VirtualMachine *get_machine() const { return machine; }

    private:
        VirtualMachine *machine;

        ThreadLocalHeap refcounted_heap;

        std::vector<Value> stack;
        std::deque<HeapObject *> zero_count_table;

        static thread_local ThreadState *current_thread;
    };

    inline ThreadState *active_thread() { return ThreadState::get_active(); }

    template <typename T, typename... Args>
    T *make_refcounted_raw(Args &&...args)
    {
        return active_thread()->make_refcounted_raw<T>(
            std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    TValue<T> make_refcounted_value(Args &&...args)
    {
        return active_thread()->make_refcounted_value<T>(
            std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    T *make_refcounted_object_raw(Args &&...args)
    {
        return active_thread()->make_refcounted_object_raw<T>(
            std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    TValue<T> make_refcounted_object_value(Args &&...args)
    {
        return active_thread()->make_refcounted_object_value<T>(
            std::forward<Args>(args)...);
    }

}  // namespace cl

#endif  // CL_THREAD_STATE_H
