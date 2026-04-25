#include "class_object.h"
#include "builtin_function.h"
#include "function.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"

namespace cl
{

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _instance_inline_slot_count, Value _base)
        : Object(&klass, compact_layout()), name(_name),
          instance_inline_slot_count(_instance_inline_slot_count),
          method_version(0), base(_base), initial_shape(Value::None())
    {
        TValue<String> dunder_class_name =
            ThreadState::get_active()
                ->get_machine()
                ->get_or_create_interned_string_value(L"__class__");
        DescriptorFlags flags = descriptor_flag(DescriptorFlag::ReadOnly);
        flags |= descriptor_flag(DescriptorFlag::StableSlot);
        initial_shape = Value::from_oop(Shape::make_root_with_single_descriptor(
            Value::from_oop(this), dunder_class_name,
            DescriptorInfo::make(StorageLocation{0, StorageKind::Inline},
                                 flags),
            1));
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

    void ClassObject::set_own_property(TValue<String> name, Value value)
    {
        set_member(name, value);
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
        return delete_member(name);
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

}  // namespace cl
