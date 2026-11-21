#include "object_model/cell.h"
#include "object_model/class_object.h"
#include "runtime/virtual_machine.h"

namespace cl
{
    BuiltinClassDefinition make_cell_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Cell};
        ClassObject *cls = ClassObject::make_builtin_class<Cell>(
            vm->get_or_create_interned_string_value(L"cell"), 0, nullptr, 0,
            vm->object_class(), SubclassPolicy::Disallowed);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Internal);
    }
}  // namespace cl
