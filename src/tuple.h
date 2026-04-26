#ifndef CL_TUPLE_H
#define CL_TUPLE_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned_typed_value.h"
#include "runtime_helpers.h"
#include "value.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl
{
    class ClassObject;

    class Tuple : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::Tuple;

        Tuple(HeapLayout layout, BootstrapObjectTag, size_t size);
        Tuple(HeapLayout layout, ClassObject *cls, size_t size);

        size_t size() const
        {
            return static_cast<size_t>(size_value.extract());
        }
        bool empty() const { return size() == 0; }

        Value item_unchecked(size_t idx) const
        {
            assert(idx < size());
            return elements[idx];
        }
        void initialize_item_unchecked(size_t idx, Value value);
        Value get_item(int64_t py_idx) const;

        static size_t size_for(size_t size)
        {
            size_t storage_count = storage_count_for(size);
            return sizeof(Tuple) + (storage_count - 1) * sizeof(Value);
        }

        static DynamicLayoutSpec layout_spec_for(BootstrapObjectTag,
                                                 size_t size)
        {
            return layout_spec_for_size(size);
        }

        static DynamicLayoutSpec layout_spec_for(ClassObject *, size_t size)
        {
            return layout_spec_for_size(size);
        }

        CL_DECLARE_DYNAMIC_LAYOUT_EXTENDS_WITH_VALUES(Tuple, Object, 1);

    private:
        static size_t storage_count_for(size_t size)
        {
            return size == 0 ? 1 : size;
        }

        static DynamicLayoutSpec layout_spec_for_size(size_t size)
        {
            return DynamicLayoutSpec{round_up_to_16byte_units(size_for(size)),
                                     static_fixed_value_count() + size};
        }

        size_t normalize_index(int64_t py_idx) const;
        void initialize_items(size_t size);

        MemberTValue<SMI> size_value;
        Value elements[1];
    };

    class VirtualMachine;

    template <typename T> std::vector<T *> vector_from_tuple(const Tuple *tuple)
    {
        std::vector<T *> result;
        result.reserve(tuple->size());
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            result.push_back(assume_convert_to<T>(tuple->item_unchecked(idx)));
        }
        return result;
    }

    template <typename T>
    Value tuple_from_vector(const std::vector<T *> &values)
    {
        Tuple *tuple =
            active_vm()->tuple_class() == nullptr
                ? make_internal_raw<Tuple>(BootstrapObjectTag{}, values.size())
                : make_object_raw<Tuple>(values.size());
        for(size_t idx = 0; idx < values.size(); ++idx)
        {
            tuple->initialize_item_unchecked(idx, Value::from_oop(values[idx]));
        }
        return Value::from_oop(tuple);
    }

    BuiltinClassDefinition make_tuple_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_TUPLE_H
