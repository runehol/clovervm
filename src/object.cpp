#include "object.h"
#include "class_object.h"
#include "overflow_slots.h"
#include "refcount.h"
#include "shape.h"
#include "shape_backed_object.h"
#include "thread_state.h"
#include "typed_value.h"
#include "value.h"
#include <algorithm>

namespace cl
{
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
    }

    void Object::initialize_shape_for_class(ClassObject *class_object)
    {
        assert(class_object != nullptr);
        initialize_shape(class_object->get_initial_shape());
    }

    void Object::initialize_shape(Shape *initial_shape)
    {
        assert(shape == nullptr);
        set_shape(initial_shape);

        uint32_t factory_default_inline_slot_count =
            get_shape()->get_factory_default_inline_slot_count();
        assert(factory_default_inline_slot_count >= 1);

        for(uint32_t slot_idx = 1; slot_idx < factory_default_inline_slot_count;
            ++slot_idx)
        {
            inline_slot_base()[slot_idx] = Value::not_present();
        }
    }

    Value Object::get_own_property(TValue<String> name) const
    {
        return shape_backed_object::get_own_property(this, name);
    }

    bool Object::set_own_property(TValue<String> name, Value value)
    {
        return shape_backed_object::set_own_property(this, name, value) ==
               shape_backed_object::StoreOwnPropertyResult::Stored;
    }

    bool Object::delete_own_property(TValue<String> name)
    {
        return shape_backed_object::delete_own_property(this, name);
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
