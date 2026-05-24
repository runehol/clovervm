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
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            ModuleSpecObject::kInlineSlotCount)
            .add_slot(L"name", ModuleSpecObject::kNameSlot)
            .add_slot(L"loader", ModuleSpecObject::kLoaderSlot)
            .add_slot(L"origin", ModuleSpecObject::kOriginSlot)
            .add_slot(L"submodule_search_locations",
                      ModuleSpecObject::kSubmoduleSearchLocationsSlot)
            .add_slot(L"has_location", ModuleSpecObject::kHasLocationSlot)
            .add_slot(L"parent", ModuleSpecObject::kParentSlot)
            .install(shape_flag(ShapeFlag::DisallowAttributeAddDelete));
    }

    BuiltinClassDefinition make_module_spec_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::ModuleSpecObject};
        ClassObject *cls = ClassObject::make_builtin_class<ModuleSpecObject>(
            vm->get_or_create_interned_string_value(L"module_spec"),
            ModuleSpecObject::kInlineSlotCount, nullptr, 0, vm->object_class());
        install_module_spec_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }

}  // namespace cl
