#ifndef CL_OVERFLOW_SLOTS_H
#define CL_OVERFLOW_SLOTS_H

#include "value.h"
#include <algorithm>
#include <cstdint>

namespace cl
{
    class OverflowSlots : public HeapObject
    {
    public:
        OverflowSlots(HeapLayout layout, uint32_t size, uint32_t capacity);

        static size_t size_for(uint32_t capacity)
        {
            return sizeof(OverflowSlots) +
                   sizeof(Value) * std::max<uint32_t>(capacity, 1) -
                   sizeof(Value);
        }

        static DynamicLayoutSpec layout_spec_for(uint32_t size,
                                                 uint32_t capacity)
        {
            return DynamicLayoutSpec{
                round_up_to_16byte_units(size_for(capacity)), capacity};
        }

        uint32_t get_size() const { return size; }
        uint32_t get_capacity() const { return capacity; }
        void set_size(uint32_t new_size)
        {
            assert(new_size <= capacity);
            size = new_size;
        }

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
        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(OverflowSlots, slots);
    };

}  // namespace cl

#endif  // CL_OVERFLOW_SLOTS_H
