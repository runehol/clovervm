#include "instance.h"
#include "refcount.h"
#include "thread_state.h"

namespace cl
{

    Instance::Instance(Value _cls, Value _shape)
        : Object(&klass), cls(_cls), shape(_shape), overflow(Value::None())
    {
        uint32_t inline_slot_capacity = get_shape()->get_inline_slot_capacity();
        for(uint32_t slot_idx = 0; slot_idx < inline_slot_capacity; ++slot_idx)
        {
            inline_slots[slot_idx] = Value::not_present();
        }
    }

    DynamicLayoutSpec Instance::layout_spec_for(Value cls, Value shape)
    {
        Shape *shape_ptr = shape.get_ptr<Shape>();
        uint32_t inline_slot_capacity = shape_ptr->get_inline_slot_capacity();
        return DynamicLayoutSpec{
            round_up_to_16byte_units(size_for(inline_slot_capacity)),
            3 + inline_slot_capacity};
    }

    Shape *Instance::get_shape() const
    {
        return shape.as_value().get_ptr<Shape>();
    }

    Instance::OverflowSlots *Instance::get_overflow_slots() const
    {
        if(overflow == Value::None())
        {
            return nullptr;
        }
        return overflow.as_value().get_ptr<OverflowSlots>();
    }

    Value Instance::get_own_property(TValue<String> name) const
    {
        StorageLocation location = get_shape()->resolve_own_property(name);
        if(!location.is_found())
        {
            return Value::not_present();
        }

        return read_storage_location(location);
    }

    void Instance::set_own_property(TValue<String> name, Value value)
    {
        Shape *current_shape = get_shape();
        StorageLocation location = current_shape->resolve_own_property(name);
        if(location.is_found())
        {
            write_storage_location(location, value);
            return;
        }

        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Add);
        shape = Value::from_oop(next_shape);

        StorageLocation new_location = next_shape->resolve_own_property(name);
        assert(new_location.is_found());
        write_storage_location(new_location, value);
    }

    bool Instance::delete_own_property(TValue<String> name)
    {
        Shape *current_shape = get_shape();
        StorageLocation location = current_shape->resolve_own_property(name);
        if(!location.is_found())
        {
            return false;
        }

        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Delete);
        shape = Value::from_oop(next_shape);
        write_storage_location(location, Value::not_present());
        return true;
    }

    Value Instance::read_storage_location(StorageLocation location) const
    {
        switch(location.kind)
        {
            case StorageKind::Inline:
                return inline_slots[location.physical_idx];
            case StorageKind::Overflow:
                {
                    OverflowSlots *overflow_slots = get_overflow_slots();
                    if(overflow_slots == nullptr)
                    {
                        return Value::not_present();
                    }
                    if(uint32_t(location.physical_idx) >=
                       overflow_slots->get_size())
                    {
                        return Value::not_present();
                    }
                    return overflow_slots->get(location.physical_idx);
                }
        }
        __builtin_unreachable();
    }

    void Instance::write_storage_location(StorageLocation location, Value value)
    {
        switch(location.kind)
        {
            case StorageKind::Inline:
                {
                    Value old_value = inline_slots[location.physical_idx];
                    inline_slots[location.physical_idx] = incref(value);
                    decref(old_value);
                    return;
                }
            case StorageKind::Overflow:
                {
                    OverflowSlots *overflow_slots =
                        ensure_overflow_slot(location.physical_idx);
                    overflow_slots->set(location.physical_idx, value);
                    overflow_slots->set_size(
                        std::max(overflow_slots->get_size(),
                                 uint32_t(location.physical_idx + 1)));
                    return;
                }
        }
        __builtin_unreachable();
    }

    Instance::OverflowSlots *
    Instance::ensure_overflow_slot(int32_t physical_idx)
    {
        assert(physical_idx >= 0);
        OverflowSlots *overflow_slots = get_overflow_slots();
        if(overflow_slots != nullptr &&
           uint32_t(physical_idx) < overflow_slots->get_capacity())
        {
            return overflow_slots;
        }

        uint32_t old_capacity =
            overflow_slots == nullptr ? 0 : overflow_slots->get_capacity();
        uint32_t new_capacity = std::max<uint32_t>(4, old_capacity);
        while(uint32_t(physical_idx) >= new_capacity)
        {
            new_capacity *= 2;
        }

        OverflowSlots *new_overflow_slots =
            ThreadState::get_active()->make_refcounted_raw<OverflowSlots>(
                overflow_slots == nullptr ? 0 : overflow_slots->get_size(),
                new_capacity);
        if(overflow_slots != nullptr)
        {
            for(uint32_t slot_idx = 0;
                slot_idx < overflow_slots->get_capacity(); ++slot_idx)
            {
                new_overflow_slots->set(slot_idx,
                                        overflow_slots->get(slot_idx));
            }
        }

        overflow = Value::from_oop(new_overflow_slots);
        return new_overflow_slots;
    }

    Instance::OverflowSlots::OverflowSlots(uint32_t _size, uint32_t _capacity)
        : Object(&klass), size(_size), capacity(_capacity)
    {
        assert(size <= capacity);
        for(uint32_t slot_idx = 0; slot_idx < capacity; ++slot_idx)
        {
            slots[slot_idx] = Value::not_present();
        }
    }

    void Instance::OverflowSlots::set(uint32_t slot_idx, Value value)
    {
        assert(slot_idx < capacity);
        Value old_value = slots[slot_idx];
        slots[slot_idx] = incref(value);
        decref(old_value);
    }

}  // namespace cl
