#ifndef CL_VIRTUAL_MACHINE_H
#define CL_VIRTUAL_MACHINE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "class_object.h"
#include "clover_entry.h"
#include "global_heap.h"
#include "intern_store.h"
#include "owned_typed_value.h"
#include "scope.h"
#include "str.h"
#include "value.h"

namespace cl
{
    class ThreadState;
    class CodeObject;
    struct SafepointScanRecord;

    using SafepointCallbackForTesting =
        void (*)(void *context, ThreadState *thread, Value accumulator,
                 Value *fp, CodeObject *code_object, uint32_t pc_offset,
                 const SafepointScanRecord &scan_record);

    class VirtualMachine
    {
    public:
        VirtualMachine();
        ~VirtualMachine();

        ThreadState *get_default_thread() { return threads[0].get(); }

        ThreadState *make_new_thread();
        void run_heap_reclamation();

        bool *safepoint_requested_ptr() { return &safepoint_requested_; }
        void request_safepoint() { safepoint_requested_ = true; }
        void clear_safepoint_request() { safepoint_requested_ = false; }
        void set_fire_every_safepoint_for_testing(bool enabled)
        {
            fire_every_safepoint_for_testing_ = enabled;
        }
        bool fire_every_safepoint_for_testing() const
        {
            return fire_every_safepoint_for_testing_;
        }
        void
        set_safepoint_callback_for_testing(SafepointCallbackForTesting callback,
                                           void *context)
        {
            safepoint_callback_for_testing_ = callback;
            safepoint_callback_context_for_testing_ = context;
        }

        GlobalHeap &get_refcounted_global_heap()
        {
            return refcounted_global_heap;
        }
        GlobalHeap &get_interned_global_heap() { return interned_global_heap; }

        template <typename Source>
        TValue<String> get_or_create_interned_string_value(const Source &str)
        {
            return TValue<String>::from_oop(
                get_or_create_interned_string_raw(str));
        }

        template <typename Source>
        String *get_or_create_interned_string_raw(const Source &str)
        {
            return interned_strings.get_or_create_raw_with_factory(
                str, [this](const Source &value) {
                    if(str_class_ != nullptr)
                    {
                        assert(str_instance_root_shape_ != nullptr);
                        String *string = interned_global_heap
                                             .make_global_internal_raw<String>(
                                                 str_class_, value);
                        return string;
                    }
                    return interned_global_heap
                        .make_global_internal_raw<String>(value);
                });
        }

        HeapPtr<Scope> get_builtin_scope() const { return builtin_scope; }
        Scope *builtin_scope_ptr() const { return builtin_scope.extract(); }
        Value get_range_builtin() const { return range_builtin; }
        void write_stdout(TValue<String> value);
        void set_stdout_file(FILE *file)
        {
            assert(file != nullptr);
            stdout_file_ = file;
        }

        ClassObject *class_for_native_layout(NativeLayoutId id) const
        {
            return class_for_native_layouts[static_cast<size_t>(id)];
        }
        ClassObject *type_class() const { return type_class_; }
        ClassObject *str_class() const
        {
            return class_for_native_layout(NativeLayoutId::String);
        }
        Shape *str_instance_root_shape() const
        {
            return str_instance_root_shape_;
        }
        ClassObject *list_class() const
        {
            return class_for_native_layout(NativeLayoutId::List);
        }
        ClassObject *list_iterator_class() const
        {
            return class_for_native_layout(NativeLayoutId::ListIterator);
        }
        ClassObject *tuple_class() const
        {
            return class_for_native_layout(NativeLayoutId::Tuple);
        }
        ClassObject *dict_class() const
        {
            return class_for_native_layout(NativeLayoutId::Dict);
        }
        ClassObject *function_class() const
        {
            return class_for_native_layout(NativeLayoutId::Function);
        }
        ClassObject *code_class() const
        {
            return class_for_native_layout(NativeLayoutId::CodeObject);
        }
        ClassObject *range_iterator_class() const
        {
            return class_for_native_layout(NativeLayoutId::RangeIterator);
        }
        ClassObject *tuple_iterator_class() const
        {
            return class_for_native_layout(NativeLayoutId::TupleIterator);
        }
        ClassObject *object_class() const
        {
            return class_for_native_layout(NativeLayoutId::Instance);
        }
        ClassObject *int_class() const { return int_class_; }
        ClassObject *bool_class() const { return bool_class_; }
        ClassObject *none_type_class() const { return none_type_class_; }
        Shape *smi_shape() const { return smi_shape_; }
        Shape *bool_shape() const { return bool_shape_; }
        Shape *none_shape() const { return none_shape_; }
        ALWAYSINLINE Shape *shape_for_inline_value(Value value) const
        {
            value.assert_not_vm_sentinel();
            if(value.is_smi())
            {
                return smi_shape_;
            }
            if(value.is_bool())
            {
                return bool_shape_;
            }
            if(value.is_none())
            {
                return none_shape_;
            }
            __builtin_unreachable();
        }
        TValue<String> dunder_class_name() const
        {
            assert(dunder_class_name_ != nullptr);
            return TValue<String>::from_oop(dunder_class_name_);
        }
        CodeObject *clover_function_entry_adapter(uint32_t n_args);

        template <typename T, typename... Args>
        T *make_immortal_internal_raw(Args &&...args)
        {
            T *object = interned_global_heap.make_global_internal_raw<T>(
                std::forward<Args>(args)...);
            object->refcount = -1;
            return object;
        }

        template <typename T, typename... Args>
        TValue<T> make_immortal_internal_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_immortal_internal_raw<T>(std::forward<Args>(args)...));
        }

        template <typename T, typename... Args>
        T *make_immortal_object_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<Object, T>);
            static_assert(HasNativeLayoutId<T>::value);
            ClassObject *cls = class_for_native_layout(T::native_layout_id);
            assert(cls != nullptr);
            T *object = interned_global_heap.make_global_internal_raw<T>(
                cls, std::forward<Args>(args)...);
            object->refcount = -1;
            return object;
        }

        template <typename T, typename... Args>
        TValue<T> make_immortal_object_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_immortal_object_raw<T>(std::forward<Args>(args)...));
        }

    private:
        friend class ThreadState;

        static constexpr size_t NativeLayoutCount =
            static_cast<size_t>(NativeLayoutId::Count);

        void run_safepoint_callback_for_testing(
            ThreadState *thread, Value accumulator, Value *fp,
            CodeObject *code_object, uint32_t pc_offset,
            const SafepointScanRecord &scan_record)
        {
            if(safepoint_callback_for_testing_ != nullptr)
            {
                safepoint_callback_for_testing_(
                    safepoint_callback_context_for_testing_, thread,
                    accumulator, fp, code_object, pc_offset, scan_record);
            }
        }
        void complete_safepoint();

        void register_builtin_class(const BuiltinClassDefinition &definition);
        void install_bootstrap_string_class();
        void install_bootstrap_tuple_class();
        void initialize_builtin_types();
        void initialize_builtin_scope();

        GlobalHeap refcounted_global_heap;
        GlobalHeap interned_global_heap;
        std::vector<std::unique_ptr<ThreadState>> threads;
        InternStore<std::wstring, String> interned_strings;
        ClassObject *type_class_ = nullptr;
        ClassObject *str_class_ = nullptr;
        ClassObject *int_class_ = nullptr;
        ClassObject *bool_class_ = nullptr;
        ClassObject *none_type_class_ = nullptr;
        String *dunder_class_name_ = nullptr;
        Shape *smi_shape_ = nullptr;
        Shape *bool_shape_ = nullptr;
        Shape *none_shape_ = nullptr;
        Shape *str_instance_root_shape_ = nullptr;
        std::array<ClassObject *, NativeLayoutCount> class_for_native_layouts =
            {};
        std::array<CodeObject *, MaxCloverFunctionEntryAdapterArgs + 1>
            clover_function_entry_adapters = {};
        std::vector<ClassObject *> builtin_classes;
        OwnedHeapPtr<Scope> builtin_scope;
        OwnedValue range_builtin;
        bool safepoint_requested_ = false;
        bool fire_every_safepoint_for_testing_ = false;
        SafepointCallbackForTesting safepoint_callback_for_testing_ = nullptr;
        void *safepoint_callback_context_for_testing_ = nullptr;
        FILE *stdout_file_ = stdout;
    };

}  // namespace cl

#endif  // CL_VIRTUAL_MACHINE_H
