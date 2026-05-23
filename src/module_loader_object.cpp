#include "module_loader_object.h"

#include "attribute_descriptor.h"
#include "class_object.h"
#include "runtime_helpers.h"
#include "shape.h"
#include "str.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    ModuleLoaderObject::ModuleLoaderObject(ClassObject *cls, Value _kind,
                                           Value _name, Value _path)
        : SlotObject(cls, native_layout), kind(_kind), name(_name), path(_path)
    {
    }

    static void install_module_loader_instance_root_shape(ClassObject *cls)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> kind_name = interned_string(L"kind");
        TValue<String> name_name = interned_string(L"name");
        TValue<String> path_name = interned_string(L"path");
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
            ShapeRootDescriptor{
                kind_name, DescriptorInfo::make(
                               StorageLocation{ModuleLoaderObject::kKindSlot,
                                               StorageKind::Inline},
                               slot_flags)},
            ShapeRootDescriptor{
                name_name, DescriptorInfo::make(
                               StorageLocation{ModuleLoaderObject::kNameSlot,
                                               StorageKind::Inline},
                               slot_flags)},
            ShapeRootDescriptor{
                path_name, DescriptorInfo::make(
                               StorageLocation{ModuleLoaderObject::kPathSlot,
                                               StorageKind::Inline},
                               slot_flags)},
        };
        cls->install_builtin_instance_root_shape(
            descriptors, std::size(descriptors),
            ModuleLoaderObject::kInlineSlotCount,
            shape_flag(ShapeFlag::DisallowAttributeAddDelete));
    }

    BuiltinClassDefinition make_module_loader_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ModuleLoaderObject};
        ClassObject *cls = ClassObject::make_builtin_class<ModuleLoaderObject>(
            vm->get_or_create_interned_string_value(L"module_loader"),
            ModuleLoaderObject::kInlineSlotCount, nullptr, 0,
            vm->object_class());
        install_module_loader_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }

}  // namespace cl
