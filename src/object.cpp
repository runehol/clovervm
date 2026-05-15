#include "object.h"
#include "attr.h"
#include "attribute_descriptor.h"
#include "class_object.h"
#include "native_function.h"
#include "overflow_slots.h"
#include "refcount.h"
#include "shape.h"
#include "str.h"
#include "string_builder.h"
#include "thread_state.h"
#include "typed_value.h"
#include "value.h"
#include "value_string.h"
#include "virtual_machine.h"
#include <algorithm>
#include <iterator>

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
                AttributeMutationPlan::store_existing(
                    nullptr, info.storage_location(), nullptr));
        }

        AttributeDeleteDescriptor
        lookup_existing_own_property_delete_descriptor(Object *object,
                                                       TValue<String> name)
        {
            Shape *current_shape = object->get_shape();
            if(!current_shape->allows_attribute_add_delete())
            {
                return AttributeDeleteDescriptor::disallowed();
            }

            int32_t descriptor_idx =
                current_shape->lookup_descriptor_index(name);
            if(descriptor_idx < 0)
            {
                return AttributeDeleteDescriptor::not_found();
            }

            DescriptorInfo info =
                current_shape->get_descriptor_info(descriptor_idx);
            if(info.has_flag(DescriptorFlag::ReadOnly))
            {
                return AttributeDeleteDescriptor::read_only();
            }

            Shape *next_shape = current_shape->derive_transition(
                name, ShapeTransitionVerb::Delete);
            return AttributeDeleteDescriptor::found(
                AttributeMutationPlan::delete_own_property(
                    next_shape, info.storage_location(), nullptr));
        }

    }  // namespace

    BuiltinClassDefinition make_object_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Instance};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class(
            vm->get_or_create_interned_string_value(L"object"), 0, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    static Value native_object_repr(Value self)
    {
        if(self.is_vm_sentinel())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"object.__repr__ expects an object receiver");
        }

        TValue<String> class_name =
            active_thread()->class_of_value(self)->get_name();
        StringBuilder builder;
        builder.append_char(L'<');
        builder.append_string(class_name);
        builder.append_c_str(L" object>");
        return builder.finish();
    }

    static Value native_object_str(Value self)
    {
        if(self.is_vm_sentinel())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"object.__str__ expects an object receiver");
        }
        return value_to_repr_string(self);
    }

    void install_object_class_methods(VirtualMachine *vm)
    {
        BuiltinNativeMethod methods[] = {
            builtin_native_method(L"__str__", native_object_str,
                                  L"Return str(self)."),
            builtin_native_method(L"__repr__", native_object_repr,
                                  L"Return repr(self)."),
        };
        install_builtin_native_methods(vm, vm->object_class(), methods,
                                       std::size(methods));
    }

    void Object::install_bootstrap_class(ClassObject *new_cls)
    {
        assert(new_cls != nullptr);
        if(shape == nullptr)
        {
            set_shape(new_cls->get_instance_root_shape());
            return;
        }
        set_shape(shape->clone_with_class(Value::from_oop(new_cls)));
    }

    void Object::set_shape(Shape *new_shape)
    {
        Shape *old_shape = shape;
        shape = incref(new_shape);
        decref(old_shape);
        if(native_layout_has_slots(native_layout_id()))
        {
            ensure_storage_for_shape(new_shape);
        }

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
        assert(instance_root_shape != nullptr);

        shape = incref(instance_root_shape);
        if(!native_layout_has_slots(native_layout_id()) ||
           native_layout_id() == NativeLayoutId::Instance)
        {
            return;
        }

        int32_t next_slot_index = instance_root_shape->get_next_slot_index();
        assert(next_slot_index >= 0);
        assert(uint32_t(next_slot_index) <=
               instance_root_shape->get_inline_slot_count());

        for(uint32_t slot_idx = 0; slot_idx < uint32_t(next_slot_index);
            ++slot_idx)
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
            AttributeReadPlan::from_storage(
                AttributeReadPlanPath::ReceiverOwnProperty,
                AttributeReadPlanKind::ReceiverSlot, nullptr, location,
                AttributeBindingContext::none(), nullptr),
            read_storage_location(location));
    }

    AttributeWriteDescriptor
    Object::lookup_own_attribute_write_descriptor(TValue<String> name)
    {
        return lookup_existing_own_property_write_descriptor(this, name);
    }

    AttributeDeleteDescriptor
    Object::lookup_own_attribute_delete_descriptor(TValue<String> name)
    {
        return lookup_existing_own_property_delete_descriptor(this, name);
    }

    bool Object::add_own_property(TValue<String> name, Value value)
    {
        value.assert_not_vm_sentinel();

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
        write_empty_storage_location(new_location, value);
        return true;
    }

    bool Object::define_own_property(TValue<String> name, Value value,
                                     DescriptorFlags descriptor_flags)
    {
        value.assert_not_vm_sentinel();

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
        write_empty_storage_location(new_location, value);
        return true;
    }

    bool Object::set_existing_own_property(TValue<String> name, Value value)
    {
        value.assert_not_vm_sentinel();

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
        value.assert_not_vm_sentinel();

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
        AttributeDeleteDescriptor descriptor =
            lookup_own_attribute_delete_descriptor(name);
        if(!descriptor.is_found())
        {
            return false;
        }
        return delete_attr_from_plan(Value::from_oop(this), descriptor.plan);
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
                    OverflowSlots *overflow_slots = get_overflow_slots();
                    assert(overflow_slots != nullptr);
                    assert(uint32_t(location.physical_idx) <
                           overflow_slots->get_size());
                    overflow_slots->set(location.physical_idx, value);
                    return;
                }
        }
        __builtin_unreachable();
    }

    void Object::write_empty_storage_location(StorageLocation location,
                                              Value value)
    {
        switch(location.kind)
        {
            case StorageKind::Inline:
                {
                    Value *slots = inline_slot_base();
                    uint32_t physical_idx =
                        static_cast<uint32_t>(location.physical_idx);
                    if(native_layout_id() == NativeLayoutId::Instance)
                    {
                        uint16_t initialized_slot_count =
                            native_layout_aux_count_value();
                        if(physical_idx >= initialized_slot_count)
                        {
                            assert(physical_idx < UINT16_MAX);
                            for(uint32_t slot_idx = initialized_slot_count;
                                slot_idx < physical_idx; ++slot_idx)
                            {
                                slots[slot_idx] = Value::not_present();
                            }
                            set_native_layout_aux_count(
                                static_cast<uint16_t>(physical_idx + 1));
                        }
                        else
                        {
                            assert(slots[physical_idx].is_not_present());
                        }
                    }
                    else
                    {
                        assert(slots[physical_idx].is_not_present());
                    }
                    slots[physical_idx] = incref(value);
                    return;
                }
            case StorageKind::Overflow:
                {
                    OverflowSlots *overflow_slots = get_overflow_slots();
                    assert(overflow_slots != nullptr);
                    assert(uint32_t(location.physical_idx) <
                           overflow_slots->get_size());
                    overflow_slots->set(location.physical_idx, value);
                    return;
                }
        }
        __builtin_unreachable();
    }

    void SlotObject::ensure_storage_for_shape(Shape *new_shape)
    {
        if(new_shape == nullptr)
        {
            return;
        }

        int32_t overflow_slot_count =
            new_shape->get_next_slot_index() -
            int32_t(new_shape->get_inline_slot_count());
        if(overflow_slot_count <= 0)
        {
            return;
        }

        OverflowSlots *overflow_slots =
            ensure_overflow_slot(overflow_slot_count - 1);
        if(overflow_slots->get_size() < uint32_t(overflow_slot_count))
        {
            overflow_slots->set_size(overflow_slot_count);
        }
    }

    OverflowSlots *SlotObject::ensure_overflow_slot(int32_t physical_idx)
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
