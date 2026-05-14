#ifndef CL_THREAD_STATE_H
#define CL_THREAD_STATE_H

#include <memory>
#include <vector>

#include "owned.h"
#include "owned_typed_value.h"
#include "shape.h"
#include "thread_local_heap.h"
#include "typed_value.h"
#include "value.h"
#include <type_traits>
#include <utility>

namespace cl
{
    enum class StartRule;
    enum class LanguageMode;

    class ClassObject;
    class ExceptionObject;
    class Function;
    class Scope;
    class VirtualMachine;
    class CodeObject;
    class ThreadState;
    class ReclamationRootSet;

    void
    process_zero_count_table_for_reclamation(ThreadState &thread,
                                             const ReclamationRootSet &roots);
    void scan_epoch_slabs_for_reclamation(ThreadState &thread,
                                          const ReclamationRootSet &roots);
    void process_thread_reclamation_epoch(ThreadState &thread,
                                          const ReclamationRootSet &roots);
#ifndef NDEBUG
    void validate_zero_count_table_for_reclamation(const ThreadState &thread);
    void validate_zero_count_tables_for_reclamation(
        const std::vector<std::unique_ptr<ThreadState>> &threads);
#endif

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

    struct SafepointScanRecord
    {
        Value *lowest_live_stack_slot = nullptr;
        Value accumulator_or_not_present = Value::not_present();
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

        class NoActiveThreadScope
        {
        public:
            NoActiveThreadScope() : previous_thread(ThreadState::current_thread)
            {
                ThreadState::current_thread = nullptr;
            }

            ~NoActiveThreadScope()
            {
                ThreadState::current_thread = previous_thread;
            }

        private:
            ThreadState *previous_thread;
        };

        ThreadState(VirtualMachine *_machine);

        static void add_to_active_zero_count_table_if_needed(HeapObject *obj);
        void add_to_zero_count_table_if_needed(HeapObject *obj);
        void adopt_reclamation_state_from(ThreadState &child);
        size_t zero_count_table_size() const { return zero_count_table.size(); }
        bool zero_count_table_contains_for_testing(HeapObject *obj) const;
        void drain_zero_count_table_for_testing();
        void switch_to_new_heap_slabs()
        {
            refcounted_heap.switch_to_new_slabs();
        }

        [[nodiscard]] Value run_clovervm_code_object(CodeObject *obj);
        [[nodiscard]] Value call_clovervm_function(TValue<Function> function);
        [[nodiscard]] Value call_clovervm_function(TValue<Function> function,
                                                   Value arg0);
        [[nodiscard]] Value call_clovervm_function(TValue<Function> function,
                                                   Value arg0, Value arg1);
        [[nodiscard]] Value call_clovervm_function(TValue<Function> function,
                                                   Value arg0, Value arg1,
                                                   Value arg2);
        [[nodiscard]] Value call_clovervm_method(Value receiver,
                                                 TValue<String> name);
        [[nodiscard]] Value
        call_clovervm_method(Value receiver, TValue<String> name, Value arg0);
        [[nodiscard]] Value call_clovervm_method(Value receiver,
                                                 TValue<String> name,
                                                 Value arg0, Value arg1);
        [[nodiscard]] Value call_clovervm_method(Value receiver,
                                                 TValue<String> name,
                                                 Value arg0, Value arg1,
                                                 Value arg2);
        void set_clover_frame_frontier(Value *fp)
        {
            clover_frame_frontier_ptr = fp;
        }
        Value *clover_frame_frontier() const
        {
            return clover_frame_frontier_ptr;
        }
        Value *clover_frame_sentinel() const
        {
            return clover_frame_sentinel_ptr;
        }
        const Value *clover_stack_begin() const { return stack.data(); }
        const Value *clover_stack_end() const
        {
            return stack.data() + stack.size();
        }
        ALWAYSINLINE bool safepoint_requested() const
        {
            return *safepoint_requested_ptr;
        }
        void publish_safepoint_scan_record(Value *lowest_live_stack_slot,
                                           Value accumulator_or_not_present);
        void handle_safepoint(Value accumulator, Value *fp, const uint8_t *pc,
                              CodeObject *code_object);
        const SafepointScanRecord &safepoint_scan_record() const
        {
            return safepoint_scan_record_;
        }

        bool has_pending_exception() const;
        PendingExceptionKind pending_exception_kind() const;
        void clear_pending_exception();
        [[nodiscard]] Value
        set_pending_exception_object(TValue<ExceptionObject> exception);
        [[nodiscard]] Value
        set_pending_exception_string(TValue<ClassObject> type,
                                     TValue<String> message);
        [[nodiscard]] Value
        set_pending_exception_string(TValue<ClassObject> type,
                                     const wchar_t *message);
        [[nodiscard]] Value
        set_pending_exception_none(TValue<ClassObject> type);
        [[nodiscard]] Value
        set_pending_builtin_exception_string(const wchar_t *type_name,
                                             TValue<String> message);
        [[nodiscard]] Value
        set_pending_builtin_exception_string(const wchar_t *type_name,
                                             const wchar_t *message);
        [[nodiscard]] Value
        set_pending_builtin_exception_none(const wchar_t *type_name);
        [[nodiscard]] Value set_pending_stop_iteration_no_value();
        [[nodiscard]] Value set_pending_stop_iteration_value(Value value);
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
        ALWAYSINLINE Shape *shape_of_value(Value value) const
        {
            value.assert_not_vm_sentinel();
            if(likely(value.is_ptr()))
            {
                Shape *shape = value.get_ptr<Object>()->get_shape();
                assert(shape != nullptr);
                return shape;
            }
            return shape_of_inline_value(value);
        }
        ALWAYSINLINE ClassObject *class_of_value(Value value) const
        {
            return shape_of_value(value)->get_class();
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
        CodeObject *compile_in_scope(const wchar_t *str, StartRule start_rule,
                                     const wchar_t *module_name,
                                     Scope *module_scope,
                                     LanguageMode language_mode);

        VirtualMachine *get_machine() const { return machine; }

    private:
        [[nodiscard]] Value
        call_clovervm_function_with_args(TValue<Function> function,
                                         const Value *args, uint32_t n_args);
        [[nodiscard]] Value call_clovervm_method_with_args(Value receiver,
                                                           TValue<String> name,
                                                           const Value *args,
                                                           uint32_t n_args);
        NOINLINE Shape *shape_of_inline_value(Value value) const;

        friend void process_zero_count_table_for_reclamation(
            ThreadState &thread, const ReclamationRootSet &roots);
        friend void
        scan_epoch_slabs_for_reclamation(ThreadState &thread,
                                         const ReclamationRootSet &roots);
        friend void
        process_thread_reclamation_epoch(ThreadState &thread,
                                         const ReclamationRootSet &roots);
#ifndef NDEBUG
        friend void
        validate_zero_count_table_for_reclamation(const ThreadState &thread);
        friend void validate_zero_count_tables_for_reclamation(
            const std::vector<std::unique_ptr<ThreadState>> &threads);
#endif

        VirtualMachine *machine;
        bool *safepoint_requested_ptr;

        ThreadLocalHeap refcounted_heap;

        std::vector<Value> stack;
        std::vector<HeapObject *> zero_count_table;
        PendingException pending_exception;
        SafepointScanRecord safepoint_scan_record_;
        // This thread's Clover frame frontier during native execution: the
        // newest live Clover frame available to native C++ code while the
        // interpreter is not actively carrying fp in its dispatch state. This
        // is a frame-chain anchor, not the full stack extent.
        Value *clover_frame_frontier_ptr = nullptr;
        // Permanent root frame for this thread's Clover frame chain.
        Value *clover_frame_sentinel_ptr = nullptr;

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
