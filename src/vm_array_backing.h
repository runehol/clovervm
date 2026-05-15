#ifndef CL_VM_ARRAY_BACKING_H
#define CL_VM_ARRAY_BACKING_H

#include "heap_object.h"
#include "native_layout_declarations.h"
#include "value.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cl
{
    class RawArrayBacking : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::RawArrayBacking;

        explicit RawArrayBacking(size_t storage_bytes)
            : HeapObject(native_layout), storage_bytes(storage_bytes)
        {
        }

        static size_t size_for(size_t storage_bytes)
        {
            return CL_OFFSETOF(RawArrayBacking, bytes) +
                   std::max<size_t>(storage_bytes, 1);
        }

        static size_t object_size_in_bytes(const RawArrayBacking *backing)
        {
            return size_for(backing->storage_bytes);
        }

        static DynamicLayoutSpec layout_spec_for(size_t storage_bytes)
        {
            return DynamicLayoutSpec{
                round_up_to_16byte_units(size_for(storage_bytes)), 0};
        }

        size_t storage_bytes;
        size_t storage_size_in_bytes() const { return storage_bytes; }

        alignas(std::max_align_t) uint8_t bytes[1];

        CL_DECLARE_EMPTY_VALUE_SPAN(RawArrayBacking);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(RawArrayBacking,
                                      RawArrayBacking::object_size_in_bytes);
        CL_DECLARE_DYNAMIC_LAYOUT_NO_VALUES(RawArrayBacking);
    };

    class ValueArrayBacking : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ValueArrayBacking;

        explicit ValueArrayBacking(size_t value_cell_count)
            : HeapObject(native_layout, native_aux_count_for_value_cell_count(
                                            value_cell_count))
        {
        }

        static uint16_t
        native_aux_count_for_value_cell_count(size_t value_cell_count)
        {
            assert(value_cell_count <= std::numeric_limits<uint16_t>::max());
            return static_cast<uint16_t>(value_cell_count);
        }

        static size_t size_for(size_t value_cell_count)
        {
            return CL_OFFSETOF(ValueArrayBacking, elements) +
                   sizeof(Value) * std::max<size_t>(value_cell_count, 1);
        }

        static size_t object_size_in_bytes(const ValueArrayBacking *backing)
        {
            return size_for(backing->native_layout_aux_count_value());
        }

        static DynamicLayoutSpec layout_spec_for(size_t value_cell_count)
        {
            native_aux_count_for_value_cell_count(value_cell_count);
            return DynamicLayoutSpec{
                round_up_to_16byte_units(size_for(value_cell_count)),
                value_cell_count};
        }

        Value elements[1];

        CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN(ValueArrayBacking, elements, 0);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(ValueArrayBacking,
                                      ValueArrayBacking::object_size_in_bytes);
        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(ValueArrayBacking, elements);
    };

    class HeapPtrArrayBacking : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::HeapPtrArrayBacking;

        explicit HeapPtrArrayBacking(size_t value_cell_count)
            : HeapObject(native_layout, native_aux_count_for_value_cell_count(
                                            value_cell_count))
        {
        }

        static uint16_t
        native_aux_count_for_value_cell_count(size_t value_cell_count)
        {
            assert(value_cell_count <= std::numeric_limits<uint16_t>::max());
            return static_cast<uint16_t>(value_cell_count);
        }

        static size_t size_for(size_t value_cell_count)
        {
            return CL_OFFSETOF(HeapPtrArrayBacking, elements) +
                   sizeof(HeapObject *) * std::max<size_t>(value_cell_count, 1);
        }

        static size_t object_size_in_bytes(const HeapPtrArrayBacking *backing)
        {
            return size_for(backing->native_layout_aux_count_value());
        }

        static DynamicLayoutSpec layout_spec_for(size_t value_cell_count)
        {
            native_aux_count_for_value_cell_count(value_cell_count);
            return DynamicLayoutSpec{
                round_up_to_16byte_units(size_for(value_cell_count)),
                value_cell_count};
        }

        HeapObject *elements[1];

        static_assert(sizeof(HeapObject *) == sizeof(Value));
        static_assert(alignof(HeapObject *) == alignof(Value));

        CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN(HeapPtrArrayBacking, elements, 0);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(
            HeapPtrArrayBacking, HeapPtrArrayBacking::object_size_in_bytes);
        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(HeapPtrArrayBacking, elements);
    };

}  // namespace cl

#endif  // CL_VM_ARRAY_BACKING_H
