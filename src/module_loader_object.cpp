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
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            ModuleLoaderObject::kInlineSlotCount)
            .add_slot(L"kind", ModuleLoaderObject::kKindSlot)
            .add_slot(L"name", ModuleLoaderObject::kNameSlot)
            .add_slot(L"path", ModuleLoaderObject::kPathSlot)
            .install(shape_flag(ShapeFlag::DisallowAttributeAddDelete));
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
