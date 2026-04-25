#include "shape.h"
#include "class_object.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <stdexcept>

namespace cl
{

    Shape::Shape(Value _owner_class, Value _previous_shape,
                 int32_t _next_slot_index, uint32_t _property_count)
        : Shape(_owner_class, _previous_shape, _next_slot_index,
                _property_count, shape_flag(ShapeFlag::None))
    {
    }

    Shape::Shape(Value _owner_class, Value _previous_shape,
                 int32_t _next_slot_index, uint32_t _property_count,
                 ShapeFlags _shape_flags)
        : Shape(_owner_class, _previous_shape, _next_slot_index,
                _property_count, _shape_flags, _property_count)
    {
    }

    Shape::Shape(Value _owner_class, Value _previous_shape,
                 int32_t _next_slot_index, uint32_t _property_count,
                 ShapeFlags _shape_flags, uint32_t _present_count)
        : Object(native_layout_id),
          previous_shape(_previous_shape == Value::None()
                             ? nullptr
                             : _previous_shape.get_ptr<Shape>()),
          next_slot_index(_next_slot_index), property_count_(_property_count),
          present_count_(_present_count), shape_flags(_shape_flags),
          transitions(), owner_class(_owner_class)
    {
        assert(present_count_ <= property_count_);
        for(uint32_t idx = 0; idx < property_count_; ++idx)
        {
            descriptor_names[idx] = Value::None();
            descriptor_infos()[idx] = DescriptorInfo::not_found();
        }
    }

    BuiltinClassDefinition make_shape_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Shape};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"shape"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    Shape *Shape::make_root_with_single_descriptor(Value owner_class,
                                                   TValue<String> name,
                                                   DescriptorInfo info,
                                                   int32_t next_slot_index,
                                                   ShapeFlags shape_flags)
    {
        ShapeRootDescriptor descriptor{name, info};
        return make_root_with_descriptors(owner_class, &descriptor, 1,
                                          next_slot_index, shape_flags);
    }

    Shape *Shape::make_root_with_descriptors(
        Value owner_class, const ShapeRootDescriptor *descriptors,
        uint32_t descriptor_count, int32_t next_slot_index,
        ShapeFlags shape_flags)
    {
        Shape *shape = make_refcounted_raw<Shape>(
            owner_class, Value::None(), next_slot_index, descriptor_count,
            shape_flags, descriptor_count);
        for(uint32_t descriptor_idx = 0; descriptor_idx < descriptor_count;
            ++descriptor_idx)
        {
            shape->descriptor_names[descriptor_idx] =
                incref(descriptors[descriptor_idx].name.as_value());
            shape->descriptor_infos()[descriptor_idx] =
                descriptors[descriptor_idx].info;
        }
        return shape;
    }

    ClassObject *Shape::get_owner_class() const
    {
        return owner_class.as_value().get_ptr<ClassObject>();
    }

    Shape *Shape::get_previous_shape() const { return previous_shape; }

    uint32_t Shape::get_inline_slot_count() const
    {
        if(has_flag(ShapeFlag::IsClassObject))
        {
            return get_owner_class()->get_class_inline_slot_count();
        }

        return get_factory_default_inline_slot_count();
    }

    uint32_t Shape::get_factory_default_inline_slot_count() const
    {
        return get_owner_class()->get_factory_default_inline_slot_count();
    }

    int32_t Shape::lookup_descriptor_index(TValue<String> name) const
    {
        for(uint32_t property_idx = 0; property_idx < present_count_;
            ++property_idx)
        {
            if(string_eq(name, get_property_name(property_idx)))
            {
                return property_idx;
            }
        }
        return -1;
    }

    DescriptorLookup
    Shape::lookup_descriptor_including_latent(TValue<String> name) const
    {
        for(uint32_t property_idx = 0; property_idx < property_count_;
            ++property_idx)
        {
            if(string_eq(name, get_property_name(property_idx)))
            {
                DescriptorPresence presence = property_idx < present_count_
                                                  ? DescriptorPresence::Present
                                                  : DescriptorPresence::Latent;
                return DescriptorLookup{presence, int32_t(property_idx),
                                        get_descriptor_info(property_idx)};
            }
        }

        return DescriptorLookup::absent();
    }

    StorageLocation Shape::resolve_present_property(TValue<String> name) const
    {
        int32_t descriptor_idx = lookup_descriptor_index(name);
        if(descriptor_idx < 0)
        {
            return StorageLocation::not_found();
        }

        return get_descriptor_info(descriptor_idx).storage_location();
    }

    StorageLocation Shape::resolve_own_property(TValue<String> name) const
    {
        return resolve_present_property(name);
    }

    Shape *Shape::lookup_transition(TValue<String> name,
                                    ShapeTransitionVerb verb,
                                    DescriptorFlags descriptor_flags) const
    {
        for(const Transition &transition: transitions)
        {
            if(transition.get_verb() == verb &&
               transition.get_descriptor_flags() == descriptor_flags &&
               string_eq(name, transition.get_name()))
            {
                return transition.get_next_shape();
            }
        }
        return nullptr;
    }

    Shape *Shape::derive_transition(TValue<String> name,
                                    ShapeTransitionVerb verb,
                                    DescriptorFlags descriptor_flags)
    {
        if(Shape *existing = lookup_transition(name, verb, descriptor_flags))
        {
            return existing;
        }

        Shape *next_shape = nullptr;
        switch(verb)
        {
            case ShapeTransitionVerb::Add:
                next_shape = derive_add_transition(name, descriptor_flags);
                break;
            case ShapeTransitionVerb::Delete:
                next_shape = derive_delete_transition(name);
                break;
        }

        transitions.emplace_back(name, verb, descriptor_flags, next_shape);
        return next_shape;
    }

    Shape *Shape::derive_add_transition(TValue<String> name,
                                        DescriptorFlags descriptor_flags)
    {
        DescriptorLookup descriptor = lookup_descriptor_including_latent(name);
        if(descriptor.is_present())
        {
            throw std::runtime_error(
                "shape add transition requires a new property");
        }

        DescriptorInfo inserted_info;
        int32_t next_slot_index_for_shape = next_slot_index;
        uint32_t next_property_count = property_count_;
        if(descriptor.is_latent())
        {
            inserted_info = descriptor.info;
        }
        else
        {
            StorageLocation storage_location;
            if(uint32_t(next_slot_index) < get_inline_slot_count())
            {
                storage_location =
                    StorageLocation{next_slot_index, StorageKind::Inline};
            }
            else
            {
                storage_location = StorageLocation{
                    next_slot_index - int32_t(get_inline_slot_count()),
                    StorageKind::Overflow};
            }
            inserted_info =
                DescriptorInfo::make(storage_location, descriptor_flags);
            next_slot_index_for_shape = next_slot_index + 1;
            next_property_count = property_count_ + 1;
        }

        Shape *next_shape = make_refcounted_raw<Shape>(
            owner_class.as_value(), Value::from_oop(this),
            next_slot_index_for_shape, next_property_count, shape_flags,
            present_count_ + 1);
        uint32_t next_property_idx = 0;
        for(uint32_t property_idx = 0; property_idx < present_count_;
            ++property_idx)
        {
            next_shape->descriptor_names[property_idx] =
                incref(get_property_name(property_idx).as_value());
            next_shape->descriptor_infos()[property_idx] =
                get_descriptor_info(property_idx);
            ++next_property_idx;
        }
        next_shape->descriptor_names[next_property_idx] =
            incref(name.as_value());
        next_shape->descriptor_infos()[next_property_idx] = inserted_info;
        ++next_property_idx;
        for(uint32_t property_idx = present_count_;
            property_idx < property_count_; ++property_idx)
        {
            if(string_eq(name, get_property_name(property_idx)))
            {
                continue;
            }

            next_shape->descriptor_names[next_property_idx] =
                incref(get_property_name(property_idx).as_value());
            next_shape->descriptor_infos()[next_property_idx] =
                get_descriptor_info(property_idx);
            ++next_property_idx;
        }
        return next_shape;
    }

    Shape *Shape::derive_delete_transition(TValue<String> name)
    {
        if(lookup_descriptor_index(name) < 0)
        {
            throw std::runtime_error(
                "shape delete transition requires an existing property");
        }

        DescriptorLookup descriptor = lookup_descriptor_including_latent(name);
        bool keep_latent = descriptor.info.has_flag(DescriptorFlag::StableSlot);
        uint32_t next_property_count =
            keep_latent ? property_count_ : property_count_ - 1;
        Shape *next_shape = make_refcounted_raw<Shape>(
            owner_class.as_value(), Value::from_oop(this), next_slot_index,
            next_property_count, shape_flags, present_count_ - 1);
        uint32_t next_property_idx = 0;
        for(uint32_t property_idx = 0; property_idx < present_count_;
            ++property_idx)
        {
            if(string_eq(name, get_property_name(property_idx)))
            {
                continue;
            }

            next_shape->descriptor_names[next_property_idx] =
                incref(get_property_name(property_idx).as_value());
            next_shape->descriptor_infos()[next_property_idx] =
                get_descriptor_info(property_idx);
            ++next_property_idx;
        }
        for(uint32_t property_idx = present_count_;
            property_idx < property_count_; ++property_idx)
        {
            next_shape->descriptor_names[next_property_idx] =
                incref(get_property_name(property_idx).as_value());
            next_shape->descriptor_infos()[next_property_idx] =
                get_descriptor_info(property_idx);
            ++next_property_idx;
        }
        if(keep_latent)
        {
            next_shape->descriptor_names[next_property_idx] =
                incref(name.as_value());
            next_shape->descriptor_infos()[next_property_idx] = descriptor.info;
            ++next_property_idx;
        }
        assert(next_property_idx == next_property_count);
        return next_shape;
    }

}  // namespace cl
