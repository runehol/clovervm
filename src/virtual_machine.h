#ifndef CL_VIRTUAL_MACHINE_H
#define CL_VIRTUAL_MACHINE_H

#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "builtin_function.h"
#include "class_object.h"
#include "heap.h"
#include "intern_store.h"
#include "owned_typed_value.h"
#include "scope.h"
#include "str.h"
#include "value.h"

namespace cl
{
    class ThreadState;

    class VirtualMachine
    {
    public:
        VirtualMachine();
        ~VirtualMachine();

        ThreadState *get_default_thread() { return threads[0].get(); }

        ThreadState *make_new_thread();

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
                        String *string =
                            interned_global_heap.make_global_raw<String>(
                                str_class_, value);
                        return string;
                    }
                    return interned_global_heap.make_global_raw<String>(value);
                });
        }

        HeapPtr<Scope> get_builtin_scope() const { return builtin_scope; }
        TValue<BuiltinFunction> get_range_builtin() const
        {
            return range_builtin;
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
        ClassObject *dict_class() const
        {
            return class_for_native_layout(NativeLayoutId::Dict);
        }
        ClassObject *function_class() const
        {
            return class_for_native_layout(NativeLayoutId::Function);
        }
        ClassObject *builtin_function_class() const
        {
            return class_for_native_layout(NativeLayoutId::BuiltinFunction);
        }
        ClassObject *code_class() const
        {
            return class_for_native_layout(NativeLayoutId::CodeObject);
        }
        ClassObject *range_iterator_class() const
        {
            return class_for_native_layout(NativeLayoutId::RangeIterator);
        }
        ClassObject *instance_class() const
        {
            return class_for_native_layout(NativeLayoutId::Instance);
        }

        template <typename T, typename... Args>
        T *make_immortal_raw(Args &&...args)
        {
            T *object = interned_global_heap.make_global_raw<T>(
                std::forward<Args>(args)...);
            object->refcount = -1;
            return object;
        }

    private:
        static constexpr size_t NativeLayoutCount =
            static_cast<size_t>(NativeLayoutId::Count);

        void register_builtin_class(const BuiltinClassDefinition &definition);
        void install_bootstrap_string_class();
        void install_bootstrap_list_class();
        void initialize_builtin_types();
        void initialize_builtin_scope();

        std::vector<std::unique_ptr<ThreadState>> threads;
        GlobalHeap refcounted_global_heap;
        GlobalHeap interned_global_heap;
        InternStore<std::wstring, String> interned_strings;
        ClassObject *type_class_ = nullptr;
        ClassObject *str_class_ = nullptr;
        Shape *str_instance_root_shape_ = nullptr;
        std::array<ClassObject *, NativeLayoutCount> class_for_native_layouts =
            {};
        std::vector<ClassObject *> builtin_classes;
        OwnedHeapPtr<Scope> builtin_scope;
        OwnedTValue<BuiltinFunction> range_builtin;
    };

}  // namespace cl

#endif  // CL_VIRTUAL_MACHINE_H
