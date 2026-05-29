#include "slot_dict.h"

#include "attr.h"
#include "class_object.h"
#include "exception_propagation.h"
#include "native_function.h"
#include "shape.h"
#include "str.h"
#include "string_builder.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    SlotDict::SlotDict(ClassObject *cls, Object *_target)
        : Object(cls, native_layout), target(_target)
    {
        assert(_target != nullptr);
        assert(native_layout_has_slots(_target->native_layout_id()));
    }

    BuiltinClassDefinition make_slotdict_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::SlotDict};
        ClassObject *cls = ClassObject::make_builtin_class<SlotDict>(
            vm->get_or_create_interned_string_value(L"slotdict"),
            SlotDict::native_static_release_count(), nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

    static Value native_slotdict_repr(ThreadState *thread, Value self)
    {
        if(!can_convert_to<SlotDict>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"slotdict.__repr__ expects a slotdict receiver");
        }

        SlotDict *dict = self.get_ptr<SlotDict>();
        StringBuilder builder;
        builder.append_char(L'{');
        bool first = true;
        for(SlotDict::EntryView entry: *dict)
        {
            if(!first)
            {
                builder.append_c_str(L", ");
            }
            first = false;
            CL_PROPAGATE_EXCEPTION(builder.append_repr(entry.key));
            builder.append_c_str(L": ");
            if(entry.value == self)
            {
                builder.append_c_str(L"{...}");
            }
            else
            {
                CL_PROPAGATE_EXCEPTION(builder.append_repr(entry.value));
            }
        }
        builder.append_char(L'}');
        return builder.finish();
    }

    static Value native_slotdict_len(ThreadState *thread, Value self)
    {
        if(!can_convert_to<SlotDict>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"slotdict.__len__ expects a slotdict receiver");
        }

        return Value::from_smi(
            static_cast<int64_t>(self.get_ptr<SlotDict>()->size()));
    }

    void install_slotdict_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_slotdict_repr,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_slotdict_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__len__", native_slotdict_len,
                                     L"Return len(self)."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->slotdict_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

    bool SlotDict::key_is_string(Value key)
    {
        return can_convert_to<String>(key);
    }

    Value SlotDict::key_type_error()
    {
        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", L"slotdict keys must be strings");
    }

    bool SlotDict::entry_at(uint32_t idx, EntryView &entry) const
    {
        AttributeMappingEntry attr_entry;
        if(!own_attribute_mapping_entry_at(get_target(), idx, attr_entry))
        {
            return false;
        }

        entry = EntryView{attr_entry.key, attr_entry.value};
        return true;
    }

    Value SlotDict::get_item(Value key) const
    {
        if(!key_is_string(key))
        {
            return key_type_error();
        }
        TValue<String> name = TValue<String>::from_value_assumed(key);

        return load_own_attribute_mapping_entry(get_target(), name);
    }

    Value SlotDict::set_item(Value key, Value value)
    {
        value.assert_not_vm_sentinel();
        if(!key_is_string(key))
        {
            return key_type_error();
        }
        TValue<String> name = TValue<String>::from_value_assumed(key);

        Object *object = get_target();
        AttributeWriteDescriptor descriptor =
            object->lookup_own_attribute_write_descriptor(name);
        if(descriptor.is_found())
        {
            if(store_attr_from_plan(Value::from_oop(object), descriptor.plan,
                                    value))
            {
                return Value::None();
            }
            if(active_thread()->has_pending_exception())
            {
                return Value::exception_marker();
            }
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError",
                L"slotdict target does not allow attribute updates");
        }
        if(descriptor.status == AttributeWriteStatus::NotFound)
        {
            if(object->add_own_property(name, value))
            {
                return Value::None();
            }
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError",
                L"slotdict target does not allow attribute updates");
        }
        if(descriptor.status == AttributeWriteStatus::ReadOnly)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"cannot assign to read-only slotdict entry");
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", L"slotdict target does not allow attribute updates");
    }

    Value SlotDict::del_item(Value key)
    {
        if(!key_is_string(key))
        {
            return key_type_error();
        }
        TValue<String> name = TValue<String>::from_value_assumed(key);

        Object *object = get_target();
        AttributeDeleteDescriptor descriptor =
            object->lookup_own_attribute_delete_descriptor(name);
        if(descriptor.is_found())
        {
            delete_attr_from_plan(Value::from_oop(object), descriptor.plan);
            return Value::None();
        }
        if(descriptor.status == AttributeDeleteStatus::NotFound)
        {
            return active_thread()->set_pending_builtin_exception_none(
                L"KeyError");
        }
        if(descriptor.status == AttributeDeleteStatus::ReadOnly)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"cannot delete read-only slotdict entry");
        }
        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", L"slotdict target does not allow attribute updates");
    }

    bool SlotDict::contains(Value key) const
    {
        if(!key_is_string(key))
        {
            return false;
        }
        TValue<String> name = TValue<String>::from_value_assumed(key);

        Shape *shape = get_target()->get_shape();
        int32_t descriptor_idx = shape->lookup_descriptor_index(name);
        if(descriptor_idx < 0)
        {
            return false;
        }
        AttributeMappingEntry entry;
        return own_attribute_mapping_entry_at(
            get_target(), static_cast<uint32_t>(descriptor_idx), entry);
    }

    size_t SlotDict::size() const
    {
        return count_own_attribute_mapping_entries(get_target());
    }

    SlotDict::Iterator::Iterator(const SlotDict *_dict, uint32_t _idx)
        : dict(_dict), idx(_idx)
    {
        skip_hidden_entries();
    }

    SlotDict::EntryView SlotDict::Iterator::operator*() const
    {
        EntryView entry;
        bool found = dict->entry_at(idx, entry);
        assert(found);
        (void)found;
        return entry;
    }

    SlotDict::Iterator &SlotDict::Iterator::operator++()
    {
        uint32_t present_count =
            dict->get_target()->get_shape()->present_count();
        if(idx < present_count)
        {
            ++idx;
            skip_hidden_entries();
        }
        return *this;
    }

    bool SlotDict::Iterator::operator==(const Iterator &other) const
    {
        return dict == other.dict && idx == other.idx;
    }

    bool SlotDict::Iterator::operator!=(const Iterator &other) const
    {
        return !(*this == other);
    }

    void SlotDict::Iterator::skip_hidden_entries()
    {
        EntryView entry;
        uint32_t present_count =
            dict->get_target()->get_shape()->present_count();
        while(idx < present_count && !dict->entry_at(idx, entry))
        {
            ++idx;
        }
    }

    SlotDict::Iterator SlotDict::begin() const { return Iterator(this, 0); }

    SlotDict::Iterator SlotDict::end() const
    {
        return Iterator(this, get_target()->get_shape()->present_count());
    }

}  // namespace cl
