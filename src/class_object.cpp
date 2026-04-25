#include "class_object.h"
#include "builtin_function.h"
#include "function.h"
#include "list.h"
#include "shape_backed_object.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"

namespace cl
{

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _factory_default_inline_slot_count,
                             Value _base)
        : Object(&klass, compact_layout()), name(_name), base(_base),
          initial_shape(Value::None()), shape(Value::None()),
          factory_default_inline_slot_count(_factory_default_inline_slot_count),
          method_version(0)
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
                    StorageLocation{ClassSlotClass, StorageKind::Inline},
                    class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_name_name,
                DescriptorInfo::make(
                    StorageLocation{ClassSlotName, StorageKind::Inline},
                    class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_bases_name,
                DescriptorInfo::make(
                    StorageLocation{ClassSlotBases, StorageKind::Inline},
                    class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_mro_name,
                DescriptorInfo::make(
                    StorageLocation{ClassSlotMro, StorageKind::Inline},
                    class_metadata_flags)},
        };
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject);
        shape = Value::from_oop(Shape::make_root_with_descriptors(
            Value::from_oop(this), descriptors, kClassPredefinedSlotCount,
            kClassPredefinedSlotCount, class_shape_flags));

        class_slots[ClassSlotClass] = Value::None();
        class_slots[ClassSlotName] = _name.as_value();
        class_slots[ClassSlotBases] = make_bases_list();
        class_slots[ClassSlotMro] = make_mro_list();
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

    Value ClassObject::get_member(TValue<String> name) const
    {
        Value own_property = get_own_property(name);
        if(!own_property.is_not_present())
        {
            return own_property;
        }

        if(ClassObject *base_ptr = get_base())
        {
            return base_ptr->get_member(name);
        }

        return Value::not_present();
    }

    Value ClassObject::get_own_property(TValue<String> name) const
    {
        Value predefined_property =
            shape_backed_object::get_own_property(this, name);
        if(!predefined_property.is_not_present())
        {
            return predefined_property;
        }

        int32_t member_idx = lookup_member_index_local(name);
        if(member_idx < 0)
        {
            return Value::not_present();
        }

        return members[member_idx].get_value();
    }

    void ClassObject::set_member(TValue<String> name, Value value)
    {
        int32_t member_idx = lookup_member_index_local(name);
        if(member_idx >= 0)
        {
            Value old_value = members[member_idx].get_value();
            maybe_bump_method_version_for_write(old_value, value);
            members[member_idx].set_value(value);
            return;
        }

        maybe_bump_method_version_for_write(Value::not_present(), value);
        members.emplace_back(name, value);
    }

    bool ClassObject::set_own_property(TValue<String> name, Value value)
    {
        Shape *current_shape = get_shape();
        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx >= 0)
        {
            DescriptorInfo info =
                current_shape->get_descriptor_info(descriptor_idx);
            if(info.has_flag(DescriptorFlag::ReadOnly))
            {
                return false;
            }

            write_storage_location(info.storage_location(), value);
            return true;
        }

        set_member(name, value);
        return true;
    }

    bool ClassObject::delete_member(TValue<String> name)
    {
        int32_t member_idx = lookup_member_index_local(name);
        if(member_idx < 0)
        {
            return false;
        }

        Value old_value = members[member_idx].get_value();
        maybe_bump_method_version_for_write(old_value, Value::not_present());
        members.erase(members.begin() + member_idx);
        return true;
    }

    bool ClassObject::delete_own_property(TValue<String> name)
    {
        if(get_shape()->lookup_descriptor_index(name) >= 0)
        {
            return false;
        }

        return delete_member(name);
    }

    Value ClassObject::read_storage_location(StorageLocation location) const
    {
        assert(location.kind == StorageKind::Inline);
        assert(uint32_t(location.physical_idx) < kClassPredefinedSlotCount);
        return class_slots[location.physical_idx].as_value();
    }

    void ClassObject::write_storage_location(StorageLocation location,
                                             Value value)
    {
        assert(location.kind == StorageKind::Inline);
        assert(uint32_t(location.physical_idx) < kClassPredefinedSlotCount);
        class_slots[location.physical_idx] = value;
    }

    bool ClassObject::is_method_value(Value value)
    {
        if(!value.is_ptr())
        {
            return false;
        }

        const Klass *klass = value.get_ptr<Object>()->klass;
        return klass == &Function::klass || klass == &BuiltinFunction::klass;
    }

    void ClassObject::maybe_bump_method_version_for_write(Value old_value,
                                                          Value new_value)
    {
        bool old_is_method = is_method_value(old_value);
        bool new_is_method = is_method_value(new_value);

        if(old_is_method != new_is_method)
        {
            ++method_version;
            return;
        }

        if(old_is_method && new_is_method && old_value != new_value)
        {
            ++method_version;
        }
    }

    int32_t ClassObject::lookup_member_index_local(TValue<String> name) const
    {
        for(uint32_t member_idx = 0; member_idx < members.size(); ++member_idx)
        {
            if(string_eq(name, members[member_idx].get_name()))
            {
                return member_idx;
            }
        }
        return -1;
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
