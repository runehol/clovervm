#include "object.h"
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
        enum class StoreOwnPropertyResult : uint8_t
        {
            Stored,
            ReadOnly,
            NotFound,
        };

        StoreOwnPropertyResult
        set_existing_own_property_impl(Object *object, TValue<String> name,
                                       Value value)
        {
            Shape *current_shape = object->get_shape();
            int32_t descriptor_idx =
                current_shape->lookup_descriptor_index(name);
            if(descriptor_idx < 0)
            {
                return StoreOwnPropertyResult::NotFound;
            }

            DescriptorInfo info =
                current_shape->get_descriptor_info(descriptor_idx);
            if(info.has_flag(DescriptorFlag::ReadOnly))
            {
                return StoreOwnPropertyResult::ReadOnly;
            }

            object->write_storage_location(info.storage_location(), value);
            return StoreOwnPropertyResult::Stored;
        }

        AttributeWriteEffects
        attribute_write_effects_for_target(const Object *object)
        {
            Shape *shape = object->get_shape();
            if(shape != nullptr && shape->has_flag(ShapeFlag::IsClassObject))
            {
                return attribute_write_effect(
                    AttributeWriteEffect::InvalidateLookupCellsOnTarget);
            }
            return attribute_write_effect(AttributeWriteEffect::None);
        }

        AttributeWriteResult
        stored_attribute_write_result(const Object *object,
                                      AttributeMutationKind kind)
        {
            return AttributeWriteResult{
                kind, attribute_write_effects_for_target(object)};
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
        ClassObject *cls = ClassObject::make_builtin_class(
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
                ->invalidate_lookup_validity_cells();
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

        return AttributeReadDescriptor::found(
            AttributeReadAccess::from_storage(
                AttributeReadAccessPath::ReceiverOwnProperty,
                AttributeReadAccessKind::ReceiverSlot, this, location,
                read_storage_location(location),
                AttributeBindingContext::none()),
            attribute_cache_blocker(AttributeCacheBlocker::MissingLookupCell));
    }

    AttributeWriteResult
    Object::define_own_property_with_result(TValue<String> name, Value value,
                                            DescriptorFlags descriptor_flags)
    {
        Shape *current_shape = get_shape();
        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx >= 0)
        {
            return AttributeWriteResult::not_stored();
        }
        if(!current_shape->allows_attribute_add_delete())
        {
            return AttributeWriteResult::not_stored();
        }

        Shape *next_shape = current_shape->derive_transition(
            name, ShapeTransitionVerb::Add, descriptor_flags);
        set_shape(next_shape);

        StorageLocation new_location =
            next_shape->resolve_present_property(name);
        assert(new_location.is_found());
        write_storage_location(new_location, value);
        return stored_attribute_write_result(this,
                                             AttributeMutationKind::Added);
    }

    bool Object::define_own_property(TValue<String> name, Value value,
                                     DescriptorFlags descriptor_flags)
    {
        return define_own_property_with_result(name, value, descriptor_flags)
            .is_stored();
    }

    AttributeWriteResult
    Object::set_existing_own_property_with_result(TValue<String> name,
                                                  Value value)
    {
        Shape *current_shape = get_shape();
        if(!current_shape->allows_attribute_updates())
        {
            return AttributeWriteResult::not_stored();
        }

        StoreOwnPropertyResult result =
            set_existing_own_property_impl(this, name, value);
        if(result == StoreOwnPropertyResult::NotFound)
        {
            return AttributeWriteResult::not_stored();
        }

        if(result == StoreOwnPropertyResult::ReadOnly)
        {
            return AttributeWriteResult::not_stored();
        }

        AttributeWriteResult write_result = stored_attribute_write_result(
            this, AttributeMutationKind::UpdatedExisting);
        if(has_attribute_write_effect(
               write_result.effects,
               AttributeWriteEffect::InvalidateLookupCellsOnTarget))
        {
            assume_convert_to<ClassObject>(this)
                ->invalidate_lookup_validity_cells();
        }
        return write_result;
    }

    bool Object::set_existing_own_property(TValue<String> name, Value value)
    {
        return set_existing_own_property_with_result(name, value).is_stored();
    }

    AttributeWriteResult
    Object::set_own_property_with_result(TValue<String> name, Value value)
    {
        AttributeWriteResult result =
            set_existing_own_property_with_result(name, value);
        if(result.is_stored())
        {
            return result;
        }

        return define_own_property_with_result(
            name, value, descriptor_flag(DescriptorFlag::None));
    }

    bool Object::set_own_property(TValue<String> name, Value value)
    {
        return set_own_property_with_result(name, value).is_stored();
    }

    AttributeWriteResult
    Object::delete_own_property_with_result(TValue<String> name)
    {
        Shape *current_shape = get_shape();
        if(!current_shape->allows_attribute_add_delete())
        {
            return AttributeWriteResult::not_stored();
        }

        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx < 0)
        {
            return AttributeWriteResult::not_stored();
        }

        DescriptorInfo info =
            current_shape->get_descriptor_info(descriptor_idx);
        if(info.has_flag(DescriptorFlag::ReadOnly))
        {
            return AttributeWriteResult::not_stored();
        }

        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Delete);
        set_shape(next_shape);
        write_storage_location(info.storage_location(), Value::not_present());
        return stored_attribute_write_result(this,
                                             AttributeMutationKind::Deleted);
    }

    bool Object::delete_own_property(TValue<String> name)
    {
        return delete_own_property_with_result(name).is_stored();
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
