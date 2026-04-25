#include "code_object.h"
#include "class_object.h"
#include "virtual_machine.h"

namespace cl
{
    BuiltinClassDefinition make_code_object_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::CodeObject};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"code"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids);
    }
}  // namespace cl
