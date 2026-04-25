#include "class_object.h"
#include "list.h"
#include "runtime_helpers.h"
#include "shape_backed_object.h"
#include "str.h"
#include "virtual_machine.h"
#include <algorithm>

namespace cl
{

    ClassObject::ClassObject(BootstrapObjectTag, TValue<String> _name,
                             uint32_t _factory_default_inline_slot_count,
                             Value _base, ShapeFlags class_shape_flags)
        : Object(BootstrapObjectTag{}, native_layout_id, compact_layout()),
          name(_name), base(_base), initial_shape(nullptr),
          factory_default_inline_slot_count(_factory_default_inline_slot_count)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        DescriptorFlags instance_class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly);
        instance_class_flags |= descriptor_flag(DescriptorFlag::StableSlot);
        initial_shape = Shape::make_root_with_single_descriptor(
            Value::from_oop(this), dunder_class_name,
            DescriptorInfo::make(StorageLocation{0, StorageKind::Inline},
                                 instance_class_flags),
            1);

        TValue<String> dunder_name_name = interned_string(L"__name__");
        TValue<String> dunder_bases_name = interned_string(L"__bases__");
        TValue<String> dunder_mro_name = interned_string(L"__mro__");
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
        set_shape(Shape::make_root_with_descriptors(
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

    ClassObject::ClassObject(ClassObject *metaclass, TValue<String> _name,
                             uint32_t _factory_default_inline_slot_count,
                             Value _base, ShapeFlags class_shape_flags)
        : ClassObject(BootstrapObjectTag{}, _name,
                      _factory_default_inline_slot_count, _base,
                      class_shape_flags)
    {
        install_bootstrap_class(metaclass);
    }

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _factory_default_inline_slot_count,
                             Value _base, ShapeFlags class_shape_flags)
        : ClassObject(active_vm()->type_class(), _name,
                      _factory_default_inline_slot_count, _base,
                      class_shape_flags)
    {
    }

    ClassObject *ClassObject::make_builtin_class(
        TValue<String> name, uint32_t factory_default_inline_slot_count,
        const BuiltinClassMethod *methods, uint32_t method_count, Value base)
    {
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType);
        ClassObject *type = active_vm()->type_class();
        assert(type != nullptr);
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            type, name, factory_default_inline_slot_count, base,
            class_shape_flags);

        DescriptorFlags method_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        for(uint32_t method_idx = 0; method_idx < method_count; ++method_idx)
        {
            bool stored = cls->define_own_property(methods[method_idx].name,
                                                   methods[method_idx].value,
                                                   method_flags);
            assert(stored);
            (void)stored;
        }

        return cls;
    }

    BuiltinClassDefinition make_type_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ClassObject};
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType);
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            BootstrapObjectTag{},
            vm->get_or_create_interned_string_value(L"type"),
            ClassObject::kClassInlineSlotCount, Value::None(),
            class_shape_flags);
        cls->install_bootstrap_class(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }

    Shape *ClassObject::get_initial_shape() const
    {
        return initial_shape.extract();
    }

    ClassObject *ClassObject::get_base() const
    {
        if(base == Value::None())
        {
            return nullptr;
        }
        return base.as_value().get_ptr<ClassObject>();
    }

    Value ClassObject::lookup_class_chain(TValue<String> name) const
    {
        Value own_property = get_own_property(name);
        Value mro_value = read_inline_slot(kClassSlotMro);
        if(!can_convert_to<List>(mro_value))
        {
            return own_property;
        }

        List *mro = try_convert_to<List>(mro_value);
        for(uint32_t mro_idx = 0; mro_idx < mro->size(); ++mro_idx)
        {
            Value class_value = mro->item_unchecked(mro_idx);
            ClassObject *cls = try_convert_to<ClassObject>(class_value);
            if(cls == nullptr)
            {
                continue;
            }

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

    bool ClassObject::define_own_property(TValue<String> name, Value value,
                                          DescriptorFlags descriptor_flags)
    {
        return shape_backed_object::define_own_property(this, name, value,
                                                        descriptor_flags) ==
               shape_backed_object::StoreOwnPropertyResult::Stored;
    }

    bool ClassObject::set_existing_own_property(TValue<String> name,
                                                Value value)
    {
        return shape_backed_object::set_existing_own_property(this, name,
                                                              value) ==
               shape_backed_object::StoreOwnPropertyResult::Stored;
    }

    bool ClassObject::set_own_property(TValue<String> name, Value value)
    {
        return shape_backed_object::set_own_property(this, name, value) ==
               shape_backed_object::StoreOwnPropertyResult::Stored;
    }

    bool ClassObject::delete_own_property(TValue<String> name)
    {
        return shape_backed_object::delete_own_property(this, name);
    }

    Value ClassObject::read_storage_location(StorageLocation location) const
    {
        switch(location.kind)
        {
            case StorageKind::Inline:
                assert(uint32_t(location.physical_idx) < kClassInlineSlotCount);
                return class_slots[location.physical_idx].as_value();
            case StorageKind::Overflow:
                return Object::read_storage_location(location);
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
                Object::write_storage_location(location, value);
                return;
        }
        __builtin_unreachable();
    }

    Value ClassObject::read_inline_slot(uint32_t slot_idx) const
    {
        assert(slot_idx < kClassInlineSlotCount);
        return class_slots[slot_idx].as_value();
    }

    Value ClassObject::make_bases_list() const
    {
        List *bases = active_vm()->list_class() == nullptr
                          ? make_internal_raw<List>(BootstrapObjectTag{})
                          : make_object_raw<List>();
        if(base != Value::None())
        {
            bases->append(base.as_value());
        }
        return Value::from_oop(bases);
    }

    Value ClassObject::make_mro_list() const
    {
        List *mro = active_vm()->list_class() == nullptr
                        ? make_internal_raw<List>(BootstrapObjectTag{})
                        : make_object_raw<List>();
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
