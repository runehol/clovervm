#include "shape.h"
#include "class_object.h"
#include "str.h"
#include "thread_state.h"
#include <stdexcept>

namespace cl
{

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

    uint32_t Shape::get_instance_inline_slot_count() const
    {
        return get_owner_class()->get_instance_inline_slot_count();
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

    DescriptorLookup Shape::lookup_descriptor(TValue<String> name) const
    {
        int32_t property_index = lookup_property_index(name);
        if(property_index < 0)
        {
            return DescriptorLookup::absent();
        }

        return DescriptorLookup{DescriptorPresence::Present, property_index,
                                get_property_storage_location(property_index)};
    }

    StorageLocation Shape::resolve_present_property(TValue<String> name) const
    {
        DescriptorLookup lookup = lookup_descriptor(name);
        if(!lookup.is_present())
        {
            return StorageLocation::not_found();
        }

        return lookup.storage_location;
    }

    StorageLocation Shape::resolve_own_property(TValue<String> name) const
    {
        return resolve_present_property(name);
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
        if(uint32_t(next_slot_index) < get_instance_inline_slot_count())
        {
            storage_location =
                StorageLocation{next_slot_index, StorageKind::Inline};
        }
        else
        {
            storage_location = StorageLocation{
                next_slot_index - int32_t(get_instance_inline_slot_count()),
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
