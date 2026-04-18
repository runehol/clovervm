#include "shape.h"
#include "str.h"
#include "thread_state.h"
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

    Shape *Instance::get_shape() const
    {
        return shape.as_value().get_ptr<Shape>();
    }

    Shape::Shape(Value _owner_class, Value _previous_shape,
                 uint32_t _next_physical_slot)
        : Object(&klass, compact_layout()), owner_class(_owner_class),
          previous_shape(_previous_shape),
          next_physical_slot(_next_physical_slot)
    {
    }

    ClassObject *Shape::get_owner_class() const
    {
        return owner_class.as_value().get_ptr<ClassObject>();
    }

    Shape *Shape::get_previous_shape() const
    {
        if(previous_shape == Value::None())
        {
            return nullptr;
        }
        return previous_shape.as_value().get_ptr<Shape>();
    }

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
