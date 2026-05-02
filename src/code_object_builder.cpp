#include "code_object_builder.h"

#include "thread_state.h"
#include "virtual_machine.h"

namespace cl
{
    CodeObjectBuilder::CodeObjectBuilder(
        const CompilationUnit *compilation_unit, Scope *module_scope,
        Scope *local_scope, Value name)
        : code_obj(make_object_raw<CodeObject>(compilation_unit, module_scope,
                                               local_scope, name))
    {
    }

    CodeObjectBuilder::CodeObjectBuilder(
        VirtualMachine *vm, const CompilationUnit *compilation_unit,
        Scope *module_scope, Scope *local_scope, Value name)
        : code_obj(vm->make_immortal_object_raw<CodeObject>(
              compilation_unit, module_scope, local_scope, name))
    {
    }
}  // namespace cl
