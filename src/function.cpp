#include "function.h"
#include "class_object.h"
#include "virtual_machine.h"

namespace cl
{
    BuiltinClassDefinition make_function_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Function};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"function"), 1, nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
    }
}  // namespace cl
