#include "object_model/function.h"
#include "builtin_types/str.h"
#include "object_model/attribute_descriptor.h"
#include "object_model/class_object.h"
#include "object_model/shape.h"
#include "runtime/virtual_machine.h"
#include <iterator>

namespace cl
{
    static void install_function_instance_root_shape(ClassObject *cls)
    {
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            Function::kInlineSlotCount)
            .add_slot(interned_string(L"__code__"), Function::kCodeObjectSlot,
                      descriptor_flag(DescriptorFlag::ReadOnly) |
                          descriptor_flag(DescriptorFlag::StableSlot))
            .reserve_slot(Function::kDefaultParametersSlot)
            .add_slot(L"__doc__", Function::kDocstringSlot)
            .install(shape_flag(ShapeFlag::DisallowAttributeAddDelete));
    }

    BuiltinClassDefinition make_function_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Function};
        ClassObject *cls = ClassObject::make_builtin_class<Function>(
            vm->get_or_create_interned_string_value(L"function"),
            Function::kInlineSlotCount, nullptr, 0, vm->object_class());
        install_function_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }
}  // namespace cl
