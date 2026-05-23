#include "module_spec_object.h"

#include "attribute_descriptor.h"
#include "class_object.h"
#include "runtime_helpers.h"
#include "shape.h"
#include "str.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    ModuleSpecObject::ModuleSpecObject(ClassObject *cls, Value _name,
                                       Value _loader, Value _origin,
                                       Value _submodule_search_locations,
                                       Value _has_location, Value _parent)
        : SlotObject(cls, native_layout), name(_name), loader(_loader),
          origin(_origin),
          submodule_search_locations(_submodule_search_locations),
          has_location(_has_location), parent(_parent)
    {
    }

    static void install_module_spec_instance_root_shape(ClassObject *cls)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> name_name = interned_string(L"name");
        TValue<String> loader_name = interned_string(L"loader");
        TValue<String> origin_name = interned_string(L"origin");
        TValue<String> submodule_search_locations_name =
            interned_string(L"submodule_search_locations");
        TValue<String> has_location_name = interned_string(L"has_location");
        TValue<String> parent_name = interned_string(L"parent");
        DescriptorFlags class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot) |
            descriptor_flag(DescriptorFlag::SpecialRead) |
            descriptor_flag(DescriptorFlag::SpecialMutate);
        DescriptorFlags slot_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[] = {
            ShapeRootDescriptor{
                dunder_class_name,
                DescriptorInfo::make(StorageLocation::not_found(), class_flags,
                                     DescriptorSpecialKind::ShapeClass)},
            ShapeRootDescriptor{name_name,
                                DescriptorInfo::make(
                                    StorageLocation{ModuleSpecObject::kNameSlot,
                                                    StorageKind::Inline},
                                    slot_flags)},
            ShapeRootDescriptor{
                loader_name, DescriptorInfo::make(
                                 StorageLocation{ModuleSpecObject::kLoaderSlot,
                                                 StorageKind::Inline},
                                 slot_flags)},
            ShapeRootDescriptor{
                origin_name, DescriptorInfo::make(
                                 StorageLocation{ModuleSpecObject::kOriginSlot,
                                                 StorageKind::Inline},
                                 slot_flags)},
            ShapeRootDescriptor{
                submodule_search_locations_name,
                DescriptorInfo::make(
                    StorageLocation{
                        ModuleSpecObject::kSubmoduleSearchLocationsSlot,
                        StorageKind::Inline},
                    slot_flags)},
            ShapeRootDescriptor{
                has_location_name,
                DescriptorInfo::make(
                    StorageLocation{ModuleSpecObject::kHasLocationSlot,
                                    StorageKind::Inline},
                    slot_flags)},
            ShapeRootDescriptor{
                parent_name, DescriptorInfo::make(
                                 StorageLocation{ModuleSpecObject::kParentSlot,
                                                 StorageKind::Inline},
                                 slot_flags)},
        };
        cls->install_builtin_instance_root_shape(
            descriptors, std::size(descriptors),
            ModuleSpecObject::kInlineSlotCount,
            shape_flag(ShapeFlag::DisallowAttributeAddDelete));
    }

    BuiltinClassDefinition make_module_spec_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ModuleSpecObject};
        ClassObject *cls = ClassObject::make_builtin_class<ModuleSpecObject>(
            vm->get_or_create_interned_string_value(L"module_spec"),
            ModuleSpecObject::kInlineSlotCount, nullptr, 0, vm->object_class());
        install_module_spec_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }

}  // namespace cl
