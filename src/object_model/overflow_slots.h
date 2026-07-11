#ifndef CL_OVERFLOW_SLOTS_H
#define CL_OVERFLOW_SLOTS_H

#include "memory/native_layout_declarations.h"
#include "object_model/value.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace cl
{
    class OverflowSlots : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::OverflowSlots;

        OverflowSlots(uint32_t size, uint32_t capacity);

        static uint16_t native_aux_count_for_capacity(uint32_t capacity)
        {
            assert(capacity <= std::numeric_limits<uint16_t>::max());
            return static_cast<uint16_t>(capacity);
        }

        static size_t size_for(uint32_t capacity)
        {
            return sizeof(OverflowSlots) +
                   sizeof(Value) * std::max<uint32_t>(capacity, 1) -
                   sizeof(Value);
        }
        static size_t size_for(uint32_t size, uint32_t capacity)
        {
            (void)size;
            return size_for(capacity);
        }

        static size_t object_size_in_bytes(const OverflowSlots *overflow_slots)
        {
            return size_for(overflow_slots->get_capacity());
        }

        uint32_t get_size() const { return size; }
        uint32_t get_capacity() const { return capacity; }
        void set_size(uint32_t new_size)
        {
            assert(new_size <= capacity);
            size = new_size;
        }

        Value *slot_value_base() { return slots; }
        const Value *slot_value_base() const { return slots; }

        Value get(uint32_t slot_idx) const
        {
            assert(slot_idx < capacity);
            return slots[slot_idx];
        }

        void set(uint32_t slot_idx, Value value);

    private:
        uint32_t size;
        uint32_t capacity;
        Value slots[1];

    public:
        CL_DECLARE_DYNAMIC_AUX_VALUE_SPAN(OverflowSlots, slots, 0);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(OverflowSlots,
                                      OverflowSlots::object_size_in_bytes);
    };

}  // namespace cl

#endif  // CL_OVERFLOW_SLOTS_H
