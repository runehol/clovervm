#ifndef CL_HEAP_OBJECT_SCAN_H
#define CL_HEAP_OBJECT_SCAN_H

#include "heap_object.h"
#include "refcount.h"
#include "value.h"
#include <cassert>
#include <cstdint>

namespace cl
{
    struct HeapScanDescriptor
    {
        uint32_t first_value_offset_in_words;
        uint32_t value_count;
    };

    inline HeapScanDescriptor
    heap_scan_descriptor_for_object(const HeapObject *obj)
    {
        assert(obj != nullptr);
        if(layout_is_expanded(obj->layout))
        {
            return HeapScanDescriptor{
                obj->layout & ~object_layout_expanded_bit,
                uint32_t(expanded_header_for_object(obj)->value_count)};
        }

        return HeapScanDescriptor{
            compact_layout_value_offset_in_words(obj->layout),
            compact_layout_value_count(obj->layout)};
    }

    inline Value *heap_first_value_slot(HeapObject *obj,
                                        HeapScanDescriptor descriptor)
    {
        assert(obj != nullptr);
        return reinterpret_cast<Value *>(
            reinterpret_cast<uint64_t *>(obj) +
            descriptor.first_value_offset_in_words);
    }

    inline const Value *heap_first_value_slot(const HeapObject *obj,
                                              HeapScanDescriptor descriptor)
    {
        assert(obj != nullptr);
        return reinterpret_cast<const Value *>(
            reinterpret_cast<const uint64_t *>(obj) +
            descriptor.first_value_offset_in_words);
    }

    inline void deallocate_heap_object_values(HeapObject *obj)
    {
        HeapScanDescriptor descriptor = heap_scan_descriptor_for_object(obj);
        Value *slots = heap_first_value_slot(obj, descriptor);
        for(uint32_t i = 0; i < descriptor.value_count; ++i)
        {
            Value value = slots[i];
            slots[i] = Value::not_present();
            decref(value);
        }
    }
}  // namespace cl

#endif  // CL_HEAP_OBJECT_SCAN_H
