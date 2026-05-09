#include "bool.h"

#include "class_object.h"
#include "virtual_machine.h"

namespace cl
{
    BuiltinClassDefinition make_bool_class(VirtualMachine *vm,
                                           ClassObject *int_class)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"bool"), 1, nullptr, 0,
            int_class);
        return builtin_class_definition(cls);
    }

}  // namespace cl
