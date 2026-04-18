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
                  Value::from_oop(this), Value::None(), 0)))
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
        int32_t property_idx = get_shape()->lookup_property(name);
        if(property_idx < 0)
        {
            return Value::not_present();
        }

        return read_slot_by_physical_index(
            get_shape()->get_property_physical_slot_index(property_idx));
    }

    void Instance::set_own_property(TValue<String> name, Value value)
    {
        Shape *current_shape = get_shape();
        int32_t property_idx = current_shape->lookup_property(name);
        if(property_idx >= 0)
        {
            write_slot_by_physical_index(
                current_shape->get_property_physical_slot_index(property_idx),
                value);
            return;
        }

        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Add);
        shape = Value::from_oop(next_shape);

        int32_t new_property_idx = next_shape->lookup_property(name);
        assert(new_property_idx >= 0);
        write_slot_by_physical_index(
            next_shape->get_property_physical_slot_index(new_property_idx),
            value);
    }

    bool Instance::delete_own_property(TValue<String> name)
    {
        Shape *current_shape = get_shape();
        int32_t property_idx = current_shape->lookup_property(name);
        if(property_idx < 0)
        {
            return false;
        }

        uint32_t physical_slot_index =
            current_shape->get_property_physical_slot_index(property_idx);
        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Delete);
        shape = Value::from_oop(next_shape);
        write_slot_by_physical_index(physical_slot_index, Value::not_present());
        return true;
    }

    Value
    Instance::read_slot_by_physical_index(uint32_t physical_slot_index) const
    {
        uint32_t inline_slot_capacity = get_shape()->get_inline_slot_capacity();
        if(physical_slot_index < inline_slot_capacity)
        {
            return inline_slots[physical_slot_index];
        }

        OverflowSlots *overflow_slots = get_overflow_slots();
        if(overflow_slots == nullptr)
        {
            return Value::not_present();
        }

        uint32_t overflow_slot_index =
            physical_slot_index - inline_slot_capacity;
        if(overflow_slot_index >= overflow_slots->get_size())
        {
            return Value::not_present();
        }
        return overflow_slots->get(overflow_slot_index);
    }

    void Instance::write_slot_by_physical_index(uint32_t physical_slot_index,
                                                Value value)
    {
        uint32_t inline_slot_capacity = get_shape()->get_inline_slot_capacity();
        if(physical_slot_index < inline_slot_capacity)
        {
            Value old_value = inline_slots[physical_slot_index];
            inline_slots[physical_slot_index] = incref(value);
            decref(old_value);
            return;
        }

        uint32_t overflow_slot_index =
            physical_slot_index - inline_slot_capacity;
        OverflowSlots *overflow_slots =
            ensure_overflow_slot(overflow_slot_index);
        overflow_slots->set(overflow_slot_index, value);
        overflow_slots->set_size(
            std::max(overflow_slots->get_size(), overflow_slot_index + 1));
    }

    OverflowSlots *Instance::ensure_overflow_slot(uint32_t overflow_slot_index)
    {
        OverflowSlots *overflow_slots = get_overflow_slots();
        if(overflow_slots != nullptr &&
           overflow_slot_index < overflow_slots->get_capacity())
        {
            return overflow_slots;
        }

        uint32_t old_capacity =
            overflow_slots == nullptr ? 0 : overflow_slots->get_capacity();
        uint32_t new_capacity = std::max<uint32_t>(4, old_capacity);
        while(overflow_slot_index >= new_capacity)
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
                 uint32_t _next_physical_slot)
        : Object(&klass, compact_layout()), owner_class(_owner_class),
          previous_shape(_previous_shape == Value::None()
                             ? nullptr
                             : _previous_shape.get_ptr<Shape>()),
          next_physical_slot(_next_physical_slot)
    {
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

    int32_t Shape::lookup_property(TValue<String> name) const
    {
        for(uint32_t property_idx = 0; property_idx < descriptors.size();
            ++property_idx)
        {
            if(string_eq(name, descriptors[property_idx].get_name()))
            {
                return property_idx;
            }
        }
        return -1;
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
        if(lookup_property(name) >= 0)
        {
            throw std::runtime_error(
                "shape add transition requires a new property");
        }

        Shape *next_shape =
            ThreadState::get_active()->make_refcounted_raw<Shape>(
                owner_class.as_value(), Value::from_oop(this),
                next_physical_slot + 1);
        next_shape->descriptors = descriptors;
        next_shape->descriptors.emplace_back(name, next_physical_slot);
        return next_shape;
    }

    Shape *Shape::derive_delete_transition(TValue<String> name)
    {
        if(lookup_property(name) < 0)
        {
            throw std::runtime_error(
                "shape delete transition requires an existing property");
        }

        Shape *next_shape =
            ThreadState::get_active()->make_refcounted_raw<Shape>(
                owner_class.as_value(), Value::from_oop(this),
                next_physical_slot);
        next_shape->descriptors.reserve(descriptors.size() - 1);
        for(const PropertyDescriptor &descriptor: descriptors)
        {
            if(!string_eq(name, descriptor.get_name()))
            {
                next_shape->descriptors.push_back(descriptor);
            }
        }
        return next_shape;
    }

}  // namespace cl
