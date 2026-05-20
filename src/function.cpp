#include "function.h"
#include "attribute_descriptor.h"
#include "class_object.h"
#include "shape.h"
#include "str.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    static void install_function_instance_root_shape(ClassObject *cls)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> dunder_doc_name = interned_string(L"__doc__");
        DescriptorFlags class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot) |
            descriptor_flag(DescriptorFlag::ShapeClassValue);
        DescriptorFlags docstring_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[] = {
            ShapeRootDescriptor{dunder_class_name,
                                DescriptorInfo::make(
                                    StorageLocation::not_found(), class_flags)},
            ShapeRootDescriptor{
                dunder_doc_name,
                DescriptorInfo::make(StorageLocation{Function::kDocstringSlot,
                                                     StorageKind::Inline},
                                     docstring_flags)},
        };
        cls->install_builtin_instance_root_shape(
            descriptors, std::size(descriptors), Function::kInlineSlotCount,
            shape_flag(ShapeFlag::DisallowAttributeAddDelete));
    }

    BuiltinClassDefinition make_function_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Function};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"function"),
            Function::kInlineSlotCount, nullptr, 0, vm->object_class());
        install_function_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }
}  // namespace cl
