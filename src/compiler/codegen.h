#ifndef CL_CODEGEN_H
#define CL_CODEGEN_H

#include "bytecode/code_object.h"
#include "compiler/ast.h"
#include "object_model/typed_value.h"
namespace cl
{
    enum class LanguageMode
    {
        StandardsCompliant,
        TrustedCloverExtensions
    };

    enum class ModuleResultMode
    {
        File,
        Interactive
    };

    class AstVector;
    class ModuleObject;
    class Scope;
    class String;
    template <typename T> class TValue;

    Expected<CodeObject *> codegen_module(
        const AstVector &av, TValue<String> module_name,
        LanguageMode language_mode = LanguageMode::StandardsCompliant);
    Expected<CodeObject *> codegen_module_in_module(
        const AstVector &av, ModuleObject *module,
        LanguageMode language_mode = LanguageMode::StandardsCompliant,
        ModuleResultMode result_mode = ModuleResultMode::File);

}  // namespace cl

#endif  // CL_CODEGEN_H
