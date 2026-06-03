#include "object_model/overflow_slots.h"
#include "object_model/refcount.h"

namespace cl
{
    OverflowSlots::OverflowSlots(uint32_t _size, uint32_t _capacity)
        : HeapObject(native_layout, native_aux_count_for_capacity(_capacity)),
          size(_size), capacity(_capacity)
    {
        assert(size <= capacity);
        for(uint32_t slot_idx = 0; slot_idx < capacity; ++slot_idx)
        {
            slots[slot_idx] = Value::not_present();
        }
    }

    void OverflowSlots::set(uint32_t slot_idx, Value value)
    {
        assert(slot_idx < capacity);
        Value old_value = slots[slot_idx];
        slots[slot_idx] = incref(value);
        decref(old_value);
    }

}  // namespace cl
