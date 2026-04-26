#include "class_object.h"
#include "list.h"
#include "runtime_helpers.h"
#include "str.h"
#include "virtual_machine.h"

namespace cl
{

    ClassObject::ClassObject(BootstrapObjectTag, TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             Value _base, ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : Object(BootstrapObjectTag{}, native_layout_id, compact_layout()),
          name(_name), bases(Value::not_present()), mro(Value::not_present()),
          instance_root_shape(nullptr), instance_default_inline_slot_count(
                                            _instance_default_inline_slot_count)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        DescriptorFlags instance_class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly);
        instance_class_flags |= descriptor_flag(DescriptorFlag::StableSlot);
        instance_root_shape = Shape::make_root_with_single_descriptor(
            Value::from_oop(this), dunder_class_name,
            DescriptorInfo::make(StorageLocation{0, StorageKind::Inline},
                                 instance_class_flags),
            1, instance_shape_flags);

        TValue<String> dunder_name_name = interned_string(L"__name__");
        TValue<String> dunder_bases_name = interned_string(L"__bases__");
        TValue<String> dunder_mro_name = interned_string(L"__mro__");
        DescriptorFlags class_metadata_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[kClassMetadataSlotCount] = {
            ShapeRootDescriptor{
                dunder_class_name,
                DescriptorInfo::make(StorageLocation{kClassMetadataSlotClass,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_name_name,
                DescriptorInfo::make(StorageLocation{kClassMetadataSlotName,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_bases_name,
                DescriptorInfo::make(StorageLocation{kClassMetadataSlotBases,
                                                     StorageKind::Inline},
                                     class_metadata_flags)},
            ShapeRootDescriptor{
                dunder_mro_name,
                DescriptorInfo::make(
                    StorageLocation{kClassMetadataSlotMro, StorageKind::Inline},
                    class_metadata_flags)},
        };
        set_shape(Shape::make_root_with_descriptors(
            Value::from_oop(this), descriptors, kClassMetadataSlotCount,
            kClassMetadataSlotCount, class_shape_flags));

        for(uint32_t slot_idx = 0;
            slot_idx < kClassExtraInlineAttributeSlotCount; ++slot_idx)
        {
            class_extra_inline_attribute_slots[slot_idx] = Value::not_present();
        }
        bases = make_bases_list(_base);
        mro = make_mro_list();
    }

    ClassObject::ClassObject(ClassObject *metaclass, TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             Value _base, ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : ClassObject(BootstrapObjectTag{}, _name,
                      _instance_default_inline_slot_count, _base,
                      class_shape_flags, instance_shape_flags)
    {
        install_bootstrap_class(metaclass);
    }

    ClassObject::ClassObject(TValue<String> _name,
                             uint32_t _instance_default_inline_slot_count,
                             Value _base, ShapeFlags class_shape_flags,
                             ShapeFlags instance_shape_flags)
        : ClassObject(active_vm()->type_class(), _name,
                      _instance_default_inline_slot_count, _base,
                      class_shape_flags, instance_shape_flags)
    {
    }

    ClassObject *ClassObject::make_builtin_class(
        TValue<String> name, uint32_t instance_default_inline_slot_count,
        const BuiltinClassMethod *methods, uint32_t method_count, Value base)
    {
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType);
        ClassObject *type = active_vm()->type_class();
        assert(type != nullptr);
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            type, name, instance_default_inline_slot_count, base,
            class_shape_flags, fixed_attribute_shape_flags());

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

        cls->set_shape(cls->get_shape()->clone_with_flags(
            class_shape_flags | fixed_attribute_shape_flags()));
        return cls;
    }

    void ClassObject::validate_inline_slot_layout()
    {
        static_assert(sizeof(MemberTValue<String>) == sizeof(Value));
        static_assert(sizeof(MemberValue) == sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, cls) ==
                      ClassObject::static_value_offset_in_words() *
                          sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, name) ==
                      CL_OFFSETOF(ClassObject, cls) +
                          kClassMetadataSlotName * sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, bases) ==
                      CL_OFFSETOF(ClassObject, cls) +
                          kClassMetadataSlotBases * sizeof(Value));
        static_assert(CL_OFFSETOF(ClassObject, mro) ==
                      CL_OFFSETOF(ClassObject, cls) +
                          kClassMetadataSlotMro * sizeof(Value));
        static_assert(
            CL_OFFSETOF(ClassObject, class_extra_inline_attribute_slots) ==
            CL_OFFSETOF(ClassObject, cls) +
                kClassMetadataSlotCount * sizeof(Value));
    }

    BuiltinClassDefinition make_type_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ClassObject};
        ShapeFlags class_shape_flags = shape_flag(ShapeFlag::IsClassObject) |
                                       shape_flag(ShapeFlag::IsImmutableType) |
                                       fixed_attribute_shape_flags();
        ClassObject::validate_inline_slot_layout();
        ClassObject *cls = active_vm()->make_immortal_internal_raw<ClassObject>(
            BootstrapObjectTag{},
            vm->get_or_create_interned_string_value(L"type"),
            ClassObject::kClassInlineStorageSlotCount, Value::None(),
            class_shape_flags, fixed_attribute_shape_flags());
        cls->install_bootstrap_class(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }

    Shape *ClassObject::get_instance_root_shape() const
    {
        return instance_root_shape.extract();
    }

    ClassObject *ClassObject::get_base() const
    {
        Value bases_value = inline_slot_base()[kClassMetadataSlotBases];
        if(!can_convert_to<List>(bases_value))
        {
            return nullptr;
        }

        List *bases_list = try_convert_to<List>(bases_value);
        if(bases_list->size() == 0)
        {
            return nullptr;
        }

        Value base_value = bases_list->item_unchecked(0);
        return try_convert_to<ClassObject>(base_value);
    }

    Value ClassObject::lookup_class_chain(TValue<String> name) const
    {
        Value own_property = get_own_property(name);
        Value mro_value = inline_slot_base()[kClassMetadataSlotMro];
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

    Value ClassObject::make_bases_list(Value base) const
    {
        List *bases = active_vm()->list_class() == nullptr
                          ? make_internal_raw<List>(BootstrapObjectTag{})
                          : make_object_raw<List>();
        if(base != Value::None())
        {
            bases->append(base);
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
