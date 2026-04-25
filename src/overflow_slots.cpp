#include "overflow_slots.h"

namespace cl
{
    OverflowSlots::OverflowSlots(uint32_t _size, uint32_t _capacity)
        : HeapObject(), size(_size), capacity(_capacity)
    {
        assert(size <= capacity);
        for(uint32_t slot_idx = 0; slot_idx < capacity; ++slot_idx)
        {
            slots[slot_idx] = Value::not_present();
        }
    }

}  // namespace cl
