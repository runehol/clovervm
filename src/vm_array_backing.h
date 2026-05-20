#ifndef CL_VM_ARRAY_BACKING_H
#define CL_VM_ARRAY_BACKING_H

#include "heap_object.h"
#include "native_layout_declarations.h"
#include "owned.h"
#include "value.h"
#include "value_state.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

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

        size_t storage_bytes;
        size_t storage_size_in_bytes() const { return storage_bytes; }

        alignas(std::max_align_t) uint8_t bytes[1];

        CL_DECLARE_EMPTY_VALUE_SPAN(RawArrayBacking);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(RawArrayBacking,
                                      RawArrayBacking::object_size_in_bytes);
    };

    class ValueArrayBacking : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ValueArrayBacking;

        explicit ValueArrayBacking(size_t value_cell_count)
            : HeapObject(native_layout),
              value_cell_count_value(TValue2<SMI>::from_smi(
                  static_cast<int64_t>(value_cell_count)))
        {
        }

        size_t value_cell_count() const
        {
            return static_cast<size_t>(value_cell_count_value.extract());
        }

        void set_value_cell_count(size_t value_cell_count)
        {
            value_cell_count_value =
                TValue2<SMI>::from_smi(static_cast<int64_t>(value_cell_count));
        }

        static size_t size_for(size_t value_cell_count)
        {
            return CL_OFFSETOF(ValueArrayBacking, elements) +
                   sizeof(Value) * std::max<size_t>(value_cell_count, 1);
        }

        static size_t object_size_in_bytes(const ValueArrayBacking *backing)
        {
            return size_for(backing->value_cell_count());
        }

        Member<TValue2<SMI>> value_cell_count_value;
        Value elements[1];

        CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN(ValueArrayBacking,
                                          value_cell_count_value, elements, 0);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(ValueArrayBacking,
                                      ValueArrayBacking::object_size_in_bytes);
    };

    class HeapPtrArrayBacking : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::HeapPtrArrayBacking;

        explicit HeapPtrArrayBacking(size_t value_cell_count)
            : HeapObject(native_layout),
              value_cell_count_value(TValue2<SMI>::from_smi(
                  static_cast<int64_t>(value_cell_count)))
        {
        }

        size_t value_cell_count() const
        {
            return static_cast<size_t>(value_cell_count_value.extract());
        }

        void set_value_cell_count(size_t value_cell_count)
        {
            value_cell_count_value =
                TValue2<SMI>::from_smi(static_cast<int64_t>(value_cell_count));
        }

        static size_t size_for(size_t value_cell_count)
        {
            return CL_OFFSETOF(HeapPtrArrayBacking, elements) +
                   sizeof(HeapObject *) * std::max<size_t>(value_cell_count, 1);
        }

        static size_t object_size_in_bytes(const HeapPtrArrayBacking *backing)
        {
            return size_for(backing->value_cell_count());
        }

        Member<TValue2<SMI>> value_cell_count_value;
        HeapObject *elements[1];

        static_assert(sizeof(HeapObject *) == sizeof(Value));
        static_assert(alignof(HeapObject *) == alignof(Value));

        CL_DECLARE_DYNAMIC_SMI_VALUE_SPAN(HeapPtrArrayBacking,
                                          value_cell_count_value, elements, 0);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(
            HeapPtrArrayBacking, HeapPtrArrayBacking::object_size_in_bytes);
    };

}  // namespace cl

#endif  // CL_VM_ARRAY_BACKING_H
