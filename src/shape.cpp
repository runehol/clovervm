#include "shape.h"
#include "refcount.h"
#include "str.h"
#include "thread_state.h"
#include <algorithm>
#include <stdexcept>

namespace cl
{

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _inline_slot_capacity)
        : Object(&klass, compact_layout()), name(_name),
          inline_slot_capacity(_inline_slot_capacity),
          initial_shape(Value::from_oop(
              ThreadState::get_active()->make_refcounted_raw<Shape>(
                  Value::from_oop(this), Value::None(), 0, 0)))
    {
    }

    Shape *ClassObject::get_initial_shape() const
    {
        return initial_shape.as_value().get_ptr<Shape>();
    }

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

    OverflowSlots *Instance::get_overflow_slots() const
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

    OverflowSlots *Instance::ensure_overflow_slot(int32_t physical_idx)
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

    OverflowSlots::OverflowSlots(uint32_t _size, uint32_t _capacity)
        : Object(&klass), size(_size), capacity(_capacity)
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

    Shape::Shape(Value _owner_class, Value _previous_shape,
                 int32_t _next_slot_index, uint32_t _property_count)
        : Object(&klass),
          previous_shape(_previous_shape == Value::None()
                             ? nullptr
                             : _previous_shape.get_ptr<Shape>()),
          next_slot_index(_next_slot_index), property_count_(_property_count),
          transitions(), owner_class(_owner_class)
    {
        for(uint32_t idx = 0; idx < property_count_; ++idx)
        {
            descriptor_names[idx] = Value::None();
        }
    }

    ClassObject *Shape::get_owner_class() const
    {
        return owner_class.as_value().get_ptr<ClassObject>();
    }

    Shape *Shape::get_previous_shape() const { return previous_shape; }

    uint32_t Shape::get_inline_slot_capacity() const
    {
        return get_owner_class()->get_inline_slot_capacity();
    }

    int32_t Shape::lookup_property_index(TValue<String> name) const
    {
        for(uint32_t property_idx = 0; property_idx < property_count_;
            ++property_idx)
        {
            if(string_eq(name, get_property_name(property_idx)))
            {
                return property_idx;
            }
        }
        return -1;
    }

    StorageLocation Shape::resolve_own_property(TValue<String> name) const
    {
        int32_t property_index = lookup_property_index(name);
        if(property_index < 0)
        {
            return StorageLocation::not_found();
        }

        return get_property_storage_location(property_index);
    }

    Shape *Shape::lookup_transition(TValue<String> name,
                                    ShapeTransitionVerb verb) const
    {
        for(const Transition &transition: transitions)
        {
            if(transition.get_verb() == verb &&
               string_eq(name, transition.get_name()))
            {
                return transition.get_next_shape();
            }
        }
        return nullptr;
    }

    Shape *Shape::derive_transition(TValue<String> name,
                                    ShapeTransitionVerb verb)
    {
        if(Shape *existing = lookup_transition(name, verb))
        {
            return existing;
        }

        Shape *next_shape = nullptr;
        switch(verb)
        {
            case ShapeTransitionVerb::Add:
                next_shape = derive_add_transition(name);
                break;
            case ShapeTransitionVerb::Delete:
                next_shape = derive_delete_transition(name);
                break;
        }

        transitions.emplace_back(name, verb, next_shape);
        return next_shape;
    }

    Shape *Shape::derive_add_transition(TValue<String> name)
    {
        if(lookup_property_index(name) >= 0)
        {
            throw std::runtime_error(
                "shape add transition requires a new property");
        }

        StorageLocation storage_location;
        if(uint32_t(next_slot_index) < get_inline_slot_capacity())
        {
            storage_location =
                StorageLocation{next_slot_index, StorageKind::Inline};
        }
        else
        {
            storage_location = StorageLocation{
                next_slot_index - int32_t(get_inline_slot_capacity()),
                StorageKind::Overflow};
        }

        Shape *next_shape =
            ThreadState::get_active()->make_refcounted_raw<Shape>(
                owner_class.as_value(), Value::from_oop(this),
                next_slot_index + 1, property_count_ + 1);
        for(uint32_t property_idx = 0; property_idx < property_count_;
            ++property_idx)
        {
            next_shape->descriptor_names[property_idx] =
                incref(get_property_name(property_idx).as_value());
            next_shape->descriptor_storage_locations()[property_idx] =
                get_property_storage_location(property_idx);
        }
        next_shape->descriptor_names[property_count_] = incref(name.as_value());
        next_shape->descriptor_storage_locations()[property_count_] =
            storage_location;
        return next_shape;
    }

    Shape *Shape::derive_delete_transition(TValue<String> name)
    {
        if(lookup_property_index(name) < 0)
        {
            throw std::runtime_error(
                "shape delete transition requires an existing property");
        }

        Shape *next_shape =
            ThreadState::get_active()->make_refcounted_raw<Shape>(
                owner_class.as_value(), Value::from_oop(this), next_slot_index,
                property_count_ - 1);
        uint32_t next_property_idx = 0;
        for(uint32_t property_idx = 0; property_idx < property_count_;
            ++property_idx)
        {
            if(string_eq(name, get_property_name(property_idx)))
            {
                continue;
            }

            next_shape->descriptor_names[next_property_idx] =
                incref(get_property_name(property_idx).as_value());
            next_shape->descriptor_storage_locations()[next_property_idx] =
                get_property_storage_location(property_idx);
            ++next_property_idx;
        }
        return next_shape;
    }

}  // namespace cl
