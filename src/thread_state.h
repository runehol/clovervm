#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <deque>
#include <memory>
#include <vector>

#include "heap.h"
#include "owned.h"
#include "owned_typed_value.h"
#include "typed_value.h"
#include "value.h"
#include <type_traits>
#include <utility>

namespace cl
{
    enum class StartRule;

    class ClassObject;
    class ExceptionObject;
    class VirtualMachine;
    struct CodeObject;

    ClassObject *class_for_native_layout(VirtualMachine *vm, NativeLayoutId id);

    enum class PendingExceptionKind
    {
        None,
        Object,
        StopIteration,
    };

    struct PendingException
    {
        PendingExceptionKind kind = PendingExceptionKind::None;
        MemberTValue<ExceptionObject> object;
        MemberValue stop_iteration_value;

        PendingException();
    };

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

        static void add_to_active_zero_count_table(HeapObject *obj);

        Value run(CodeObject *obj);

        bool has_pending_exception() const;
        PendingExceptionKind pending_exception_kind() const;
        void clear_pending_exception();
        void set_pending_exception_object(TValue<ExceptionObject> exception);
        void set_pending_exception_string(TValue<ClassObject> type,
                                          TValue<String> message);
        void set_pending_exception_string(TValue<ClassObject> type,
                                          const wchar_t *message);
        void set_pending_exception_none(TValue<ClassObject> type);
        void set_pending_builtin_exception_string(const wchar_t *type_name,
                                                  TValue<String> message);
        void set_pending_builtin_exception_string(const wchar_t *type_name,
                                                  const wchar_t *message);
        void set_pending_builtin_exception_none(const wchar_t *type_name);
        void set_pending_stop_iteration_no_value();
        void set_pending_stop_iteration_value(Value value);
        Value pending_exception_object() const;
        Value pending_stop_iteration_value() const;

        static ThreadState *get_active()
        {
            assert(current_thread != nullptr);
            return current_thread;
        }

        template <typename T, typename... Args>
        T *make_internal_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<HeapObject, T>);
            static_assert(HasObjectLayout<T>::value);
            return refcounted_heap.make<T>(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        TValue<T> make_internal_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_internal_raw<T>(std::forward<Args>(args)...));
        }

        ClassObject *class_for_native_layout(NativeLayoutId id) const
        {
            return cl::class_for_native_layout(machine, id);
        }
        ClassObject *class_for_builtin_name(const wchar_t *name) const;

        template <typename T, typename... Args>
        T *make_object_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            static_assert(HasNativeLayoutId<T>::value);
            ClassObject *cls = class_for_native_layout(T::native_layout_id);
            assert(cls != nullptr);
            return make_internal_raw<T>(cls, std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        TValue<T> make_object_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_object_raw<T>(std::forward<Args>(args)...));
        }

        CodeObject *compile(const wchar_t *str, StartRule start_rule);
        CodeObject *compile(const wchar_t *str, StartRule start_rule,
                            const wchar_t *module_name);

        VirtualMachine *get_machine() const { return machine; }

    private:
        VirtualMachine *machine;

        ThreadLocalHeap refcounted_heap;

        std::vector<Value> stack;
        std::deque<HeapObject *> zero_count_table;
        PendingException pending_exception;

        static thread_local ThreadState *current_thread;
    };

    inline ThreadState *active_thread() { return ThreadState::get_active(); }

    template <typename T, typename... Args> T *make_internal_raw(Args &&...args)
    {
        return active_thread()->make_internal_raw<T>(
            std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    TValue<T> make_internal_value(Args &&...args)
    {
        return active_thread()->make_internal_value<T>(
            std::forward<Args>(args)...);
    }

    template <typename T, typename... Args> T *make_object_raw(Args &&...args)
    {
        return active_thread()->make_object_raw<T>(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    TValue<T> make_object_value(Args &&...args)
    {
        return active_thread()->make_object_value<T>(
            std::forward<Args>(args)...);
    }

}  // namespace cl

#endif  // CL_THREAD_STATE_H
