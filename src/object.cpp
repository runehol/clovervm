#include "object.h"
#include "attr.h"
#include "attribute_descriptor.h"
#include "class_object.h"
#include "overflow_slots.h"
#include "refcount.h"
#include "shape.h"
#include "thread_state.h"
#include "typed_value.h"
#include "value.h"
#include "virtual_machine.h"
#include <algorithm>

namespace cl
{
    namespace
    {
        AttributeWriteDescriptor
        lookup_existing_own_property_write_descriptor(Object *object,
                                                      TValue<String> name)
        {
            Shape *current_shape = object->get_shape();
            int32_t descriptor_idx =
                current_shape->lookup_descriptor_index(name);
            if(descriptor_idx < 0)
            {
                return AttributeWriteDescriptor::not_found();
            }

            if(!current_shape->allows_attribute_updates())
            {
                return AttributeWriteDescriptor::disallowed();
            }

            DescriptorInfo info =
                current_shape->get_descriptor_info(descriptor_idx);
            if(info.has_flag(DescriptorFlag::ReadOnly))
            {
                return AttributeWriteDescriptor::read_only();
            }

            return AttributeWriteDescriptor::found(
                AttributeWritePlan::store_existing(
                    nullptr, info.storage_location(), nullptr));
        }

    }  // namespace

    void Object::validate_inline_slot_layout()
    {
        static_assert(sizeof(cls) == sizeof(Value));
        static_assert(CL_OFFSETOF(Object, cls) ==
                      Object::static_value_offset_in_words() * sizeof(Value));
    }

    BuiltinClassDefinition make_object_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Instance};
        Object::validate_inline_slot_layout();
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class(
            vm->get_or_create_interned_string_value(L"object"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    void Object::install_class(ClassObject *new_cls)
    {
        assert(new_cls != nullptr);
        cls = incref(new_cls);
    }

    TValue<ClassObject> Object::get_class() const
    {
        assert(cls != nullptr);
        return TValue<ClassObject>::from_oop(cls);
    }

    void Object::set_shape(Shape *new_shape)
    {
        Shape *old_shape = shape;
        shape = incref(new_shape);
        decref(old_shape);

        if(old_shape != nullptr && old_shape != new_shape &&
           new_shape != nullptr &&
           new_shape->has_flag(ShapeFlag::IsClassObject))
        {
            assume_convert_to<ClassObject>(this)
                ->invalidate_lookup_validity_cells_for_shape_change();
        }
    }

    void Object::initialize_shape_for_class(ClassObject *class_object)
    {
        assert(class_object != nullptr);
        initialize_shape(class_object->get_instance_root_shape());
    }

    void Object::initialize_shape(Shape *instance_root_shape)
    {
        assert(shape == nullptr);
        set_shape(instance_root_shape);

        uint32_t instance_default_inline_slot_count =
            get_shape()->get_instance_default_inline_slot_count();
        assert(instance_default_inline_slot_count >= 1);

        for(uint32_t slot_idx = 1;
            slot_idx < instance_default_inline_slot_count; ++slot_idx)
        {
            inline_slot_base()[slot_idx] = Value::not_present();
        }
    }

    Value Object::get_own_property(TValue<String> name) const
    {
        StorageLocation location = get_shape()->resolve_present_property(name);
        if(!location.is_found())
        {
            return Value::not_present();
        }

        return read_storage_location(location);
    }

    AttributeReadDescriptor
    Object::lookup_own_attribute_descriptor(TValue<String> name) const
    {
        StorageLocation location = get_shape()->resolve_present_property(name);
        if(!location.is_found())
        {
            return AttributeReadDescriptor::not_found();
        }

        return AttributeReadDescriptor::found(AttributeReadPlan::from_storage(
            AttributeReadPlanPath::ReceiverOwnProperty,
            AttributeReadPlanKind::ReceiverSlot, nullptr, location,
            read_storage_location(location), AttributeBindingContext::none(),
            nullptr));
    }

    AttributeWriteDescriptor
    Object::lookup_own_attribute_write_descriptor(TValue<String> name)
    {
        return lookup_existing_own_property_write_descriptor(this, name);
    }

    bool Object::add_own_property(TValue<String> name, Value value)
    {
        Shape *current_shape = get_shape();
        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx >= 0)
        {
            return false;
        }
        if(!current_shape->allows_attribute_add_delete())
        {
            return false;
        }

        Shape *next_shape = current_shape->derive_transition(
            name, ShapeTransitionVerb::Add,
            descriptor_flag(DescriptorFlag::None));
        set_shape(next_shape);

        StorageLocation new_location =
            next_shape->resolve_present_property(name);
        assert(new_location.is_found());
        write_storage_location(new_location, value);
        return true;
    }

    bool Object::define_own_property(TValue<String> name, Value value,
                                     DescriptorFlags descriptor_flags)
    {
        Shape *current_shape = get_shape();
        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx >= 0)
        {
            return false;
        }
        if(!current_shape->allows_attribute_add_delete())
        {
            return false;
        }

        Shape *next_shape = current_shape->derive_transition(
            name, ShapeTransitionVerb::Add, descriptor_flags);
        set_shape(next_shape);

        StorageLocation new_location =
            next_shape->resolve_present_property(name);
        assert(new_location.is_found());
        write_storage_location(new_location, value);
        return true;
    }

    bool Object::set_existing_own_property(TValue<String> name, Value value)
    {
        AttributeWriteDescriptor descriptor =
            lookup_existing_own_property_write_descriptor(this, name);
        if(!descriptor.is_found())
        {
            return false;
        }
        return store_attr_from_plan(Value::from_oop(this), descriptor.plan,
                                    value);
    }

    bool Object::set_own_property(TValue<String> name, Value value)
    {
        AttributeWriteDescriptor descriptor =
            lookup_own_attribute_write_descriptor(name);
        if(descriptor.is_found())
        {
            return store_attr_from_plan(Value::from_oop(this), descriptor.plan,
                                        value);
        }
        if(descriptor.status == AttributeWriteStatus::NotFound)
        {
            return add_own_property(name, value);
        }
        return false;
    }

    bool Object::delete_own_property(TValue<String> name)
    {
        Shape *current_shape = get_shape();
        if(!current_shape->allows_attribute_add_delete())
        {
            return false;
        }

        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx < 0)
        {
            return false;
        }

        DescriptorInfo info =
            current_shape->get_descriptor_info(descriptor_idx);
        if(info.has_flag(DescriptorFlag::ReadOnly))
        {
            return false;
        }

        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Delete);
        set_shape(next_shape);
        write_storage_location(info.storage_location(), Value::not_present());
        return true;
    }

    Value Object::read_storage_location(StorageLocation location) const
    {
        switch(location.kind)
        {
            case StorageKind::Inline:
                return inline_slot_base()[location.physical_idx];
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

    void Object::write_storage_location(StorageLocation location, Value value)
    {
        switch(location.kind)
        {
            case StorageKind::Inline:
                {
                    Value *slots = inline_slot_base();
                    Value old_value = slots[location.physical_idx];
                    slots[location.physical_idx] = incref(value);
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

    OverflowSlots *Object::ensure_overflow_slot(int32_t physical_idx)
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

        OverflowSlots *new_overflow_slots = make_internal_raw<OverflowSlots>(
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

        OverflowSlots *old_overflow_storage = overflow_storage;
        overflow_storage = incref(new_overflow_slots);
        decref(old_overflow_storage);
        return new_overflow_slots;
    }

}  // namespace cl
