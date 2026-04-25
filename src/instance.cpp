#include "instance.h"
#include "class_object.h"
#include "refcount.h"
#include "runtime_helpers.h"
#include "shape_backed_object.h"
#include "virtual_machine.h"

namespace cl
{

    Instance::Instance(Value _cls, Shape *_shape)
        : Object(_cls.get_ptr<ClassObject>(), native_layout_id)
    {
        incref(Object::get_class());
        set_shape(_shape);
        uint32_t factory_default_inline_slot_count =
            get_shape()->get_factory_default_inline_slot_count();
        for(uint32_t slot_idx = 1; slot_idx < factory_default_inline_slot_count;
            ++slot_idx)
        {
            inline_slot_base()[slot_idx] = Value::not_present();
        }

        TValue<String> dunder_class_name = interned_string(L"__class__");
        StorageLocation class_location =
            get_shape()->resolve_present_property(dunder_class_name);
        assert(class_location.is_found());
        assert(class_location.kind == StorageKind::Inline);
        assert(class_location.physical_idx == 0);
    }

    BuiltinClassDefinition make_instance_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Instance};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"object"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }

    DynamicLayoutSpec Instance::layout_spec_for(Value cls, Shape *shape)
    {
        uint32_t factory_default_inline_slot_count =
            shape->get_factory_default_inline_slot_count();
        assert(factory_default_inline_slot_count >= 1);
        uint32_t dynamic_inline_slot_count =
            factory_default_inline_slot_count - 1;
        return DynamicLayoutSpec{
            round_up_to_16byte_units(size_for(dynamic_inline_slot_count)),
            factory_default_inline_slot_count};
    }

    Value Instance::get_class() const
    {
        return Value::from_oop(Object::get_class());
    }

    Shape *Instance::get_shape() const { return shape; }

    void Instance::set_shape(Shape *new_shape)
    {
        Shape *old_shape = shape;
        shape = incref(new_shape);
        decref(old_shape);
    }

    OverflowSlots *Instance::get_overflow_slots() const
    {
        if(overflow_storage == nullptr)
        {
            return nullptr;
        }
        return static_cast<OverflowSlots *>(overflow_storage);
    }

    Value Instance::get_own_property(TValue<String> name) const
    {
        return shape_backed_object::get_own_property(this, name);
    }

    bool Instance::set_own_property(TValue<String> name, Value value)
    {
        return shape_backed_object::set_own_property(this, name, value) ==
               shape_backed_object::StoreOwnPropertyResult::Stored;
    }

    bool Instance::delete_own_property(TValue<String> name)
    {
        return shape_backed_object::delete_own_property(this, name);
    }

    Value Instance::read_storage_location(StorageLocation location) const
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

    void Instance::write_storage_location(StorageLocation location, Value value)
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

        HeapObject *old_overflow_storage = overflow_storage;
        overflow_storage = incref(new_overflow_slots);
        decref(old_overflow_storage);
        return new_overflow_slots;
    }

}  // namespace cl
