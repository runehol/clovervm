#include "class_object.h"
#include "list.h"
#include "shape_backed_object.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <algorithm>

namespace cl
{

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _factory_default_inline_slot_count,
                             Value _base)
        : Object(&klass, compact_layout()), name(_name), base(_base),
          initial_shape(Value::None()), shape(Value::None()),
          overflow(Value::None()),
          factory_default_inline_slot_count(_factory_default_inline_slot_count)
    {
        VirtualMachine *vm = ThreadState::get_active()->get_machine();
        TValue<String> dunder_class_name =
            vm->get_or_create_interned_string_value(L"__class__");
        DescriptorFlags instance_class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly);
        instance_class_flags |= descriptor_flag(DescriptorFlag::StableSlot);
        initial_shape = Value::from_oop(Shape::make_root_with_single_descriptor(
            Value::from_oop(this), dunder_class_name,
            DescriptorInfo::make(StorageLocation{0, StorageKind::Inline},
                                 instance_class_flags),
            1));

        TValue<String> dunder_name_name =
            vm->get_or_create_interned_string_value(L"__name__");
        TValue<String> dunder_bases_name =
            vm->get_or_create_interned_string_value(L"__bases__");
        TValue<String> dunder_mro_name =
            vm->get_or_create_interned_string_value(L"__mro__");
        DescriptorFlags class_metadata_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[kClassPredefinedSlotCount] = {
            ShapeRootDescriptor{
                dunder_class_name,
                DescriptorInfo::make(
                    StorageLocation{kClassSlotClass, StorageKind::Inline},
                    class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_name_name,
                DescriptorInfo::make(
                    StorageLocation{kClassSlotName, StorageKind::Inline},
                    class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_bases_name,
                DescriptorInfo::make(
                    StorageLocation{kClassSlotBases, StorageKind::Inline},
                    class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_mro_name,
                DescriptorInfo::make(
                    StorageLocation{kClassSlotMro, StorageKind::Inline},
                    class_metadata_flags)},
        };
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject);
        shape = Value::from_oop(Shape::make_root_with_descriptors(
            Value::from_oop(this), descriptors, kClassPredefinedSlotCount,
            kClassPredefinedSlotCount, class_shape_flags));

        for(uint32_t slot_idx = 0; slot_idx < kClassInlineSlotCount; ++slot_idx)
        {
            class_slots[slot_idx] = Value::not_present();
        }
        class_slots[kClassSlotClass] = Value::None();
        class_slots[kClassSlotName] = _name.as_value();
        class_slots[kClassSlotBases] = make_bases_list();
        class_slots[kClassSlotMro] = make_mro_list();
    }

    Shape *ClassObject::get_shape() const
    {
        return shape.as_value().get_ptr<Shape>();
    }

    void ClassObject::set_shape(Shape *new_shape)
    {
        shape = Value::from_oop(new_shape);
    }

    Shape *ClassObject::get_initial_shape() const
    {
        return initial_shape.as_value().get_ptr<Shape>();
    }

    ClassObject *ClassObject::get_base() const
    {
        if(base == Value::None())
        {
            return nullptr;
        }
        return base.as_value().get_ptr<ClassObject>();
    }

    uint32_t ClassObject::member_count() const
    {
        Shape *current_shape = get_shape();
        assert(current_shape->present_count() >= kClassPredefinedSlotCount);
        return current_shape->present_count() - kClassPredefinedSlotCount;
    }

    TValue<String> ClassObject::get_member_name(uint32_t member_idx) const
    {
        return get_shape()->get_property_name(
            member_descriptor_index(member_idx));
    }

    Value ClassObject::get_member_value(uint32_t member_idx) const
    {
        DescriptorInfo info = get_shape()->get_descriptor_info(
            member_descriptor_index(member_idx));
        return read_storage_location(info.storage_location());
    }

    Value ClassObject::get_member(TValue<String> name) const
    {
        return lookup_class_chain(name);
    }

    Value ClassObject::lookup_class_chain(TValue<String> name) const
    {
        Value own_property = get_own_property(name);
        Value mro_value = read_inline_slot(kClassSlotMro);
        if(!mro_value.is_ptr() ||
           mro_value.get_ptr<Object>()->klass != &List::klass)
        {
            return own_property;
        }

        List *mro = mro_value.get_ptr<List>();
        for(uint32_t mro_idx = 0; mro_idx < mro->size(); ++mro_idx)
        {
            Value class_value = mro->item_unchecked(mro_idx);
            if(!class_value.is_ptr() ||
               class_value.get_ptr<Object>()->klass != &ClassObject::klass)
            {
                continue;
            }

            ClassObject *cls = class_value.get_ptr<ClassObject>();
            DescriptorLookup lookup =
                cls->get_shape()->lookup_descriptor_including_latent(name);
            if(!lookup.is_present())
            {
                continue;
            }

            return cls->read_storage_location(lookup.storage_location());
        }

        return Value::not_present();
    }

    Value ClassObject::get_own_property(TValue<String> name) const
    {
        return shape_backed_object::get_own_property(this, name);
    }

    void ClassObject::set_member(TValue<String> name, Value value)
    {
        store_own_property_direct(name, value);
    }

    bool ClassObject::store_own_property_direct(TValue<String> name,
                                                Value value)
    {
        return shape_backed_object::set_own_property(this, name, value) ==
               shape_backed_object::StoreOwnPropertyResult::Stored;
    }

    bool ClassObject::delete_own_property_direct(TValue<String> name)
    {
        return shape_backed_object::delete_own_property(this, name);
    }

    bool ClassObject::set_own_property(TValue<String> name, Value value)
    {
        return store_own_property_direct(name, value);
    }

    bool ClassObject::delete_member(TValue<String> name)
    {
        return delete_own_property_direct(name);
    }

    bool ClassObject::delete_own_property(TValue<String> name)
    {
        return delete_own_property_direct(name);
    }

    Value ClassObject::read_storage_location(StorageLocation location) const
    {
        switch(location.kind)
        {
            case StorageKind::Inline:
                assert(uint32_t(location.physical_idx) < kClassInlineSlotCount);
                return class_slots[location.physical_idx].as_value();
            case StorageKind::Overflow:
                {
                    Instance::OverflowSlots *overflow_slots =
                        get_overflow_slots();
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

    void ClassObject::write_storage_location(StorageLocation location,
                                             Value value)
    {
        switch(location.kind)
        {
            case StorageKind::Inline:
                {
                    assert(uint32_t(location.physical_idx) <
                           kClassInlineSlotCount);
                    class_slots[location.physical_idx] = value;
                    return;
                }
            case StorageKind::Overflow:
                {
                    Instance::OverflowSlots *overflow_slots =
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

    Value ClassObject::read_inline_slot(uint32_t slot_idx) const
    {
        assert(slot_idx < kClassInlineSlotCount);
        return class_slots[slot_idx].as_value();
    }

    uint32_t ClassObject::member_descriptor_index(uint32_t member_idx) const
    {
        uint32_t descriptor_idx = kClassPredefinedSlotCount + member_idx;
        assert(descriptor_idx < get_shape()->present_count());
        return descriptor_idx;
    }

    Instance::OverflowSlots *ClassObject::get_overflow_slots() const
    {
        if(overflow == Value::None())
        {
            return nullptr;
        }
        return overflow.as_value().get_ptr<Instance::OverflowSlots>();
    }

    Instance::OverflowSlots *
    ClassObject::ensure_overflow_slot(int32_t physical_idx)
    {
        assert(physical_idx >= 0);
        Instance::OverflowSlots *overflow_slots = get_overflow_slots();
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

        Instance::OverflowSlots *new_overflow_slots =
            ThreadState::get_active()
                ->make_refcounted_raw<Instance::OverflowSlots>(
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

    Value ClassObject::make_bases_list() const
    {
        List *bases = ThreadState::get_active()->make_refcounted_raw<List>();
        if(base != Value::None())
        {
            bases->append(base.as_value());
        }
        return Value::from_oop(bases);
    }

    Value ClassObject::make_mro_list() const
    {
        List *mro = ThreadState::get_active()->make_refcounted_raw<List>();
        mro->append(Value::from_oop(const_cast<ClassObject *>(this)));
        ClassObject *base_ptr = get_base();
        while(base_ptr != nullptr)
        {
            mro->append(Value::from_oop(base_ptr));
            base_ptr = base_ptr->get_base();
        }
        return Value::from_oop(mro);
    }

}  // namespace cl
