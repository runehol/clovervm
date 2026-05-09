#ifndef CL_CODEGEN_H
#define CL_CODEGEN_H

#include "ast.h"
#include "code_object.h"
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

    struct AstVector;
    class Scope;
    struct String;
    template <typename T> class TValue;

    CodeObject *codegen_module(
        const AstVector &av, TValue<String> module_name,
        LanguageMode language_mode = LanguageMode::StandardsCompliant);
    CodeObject *codegen_module_in_scope(
        const AstVector &av, Scope *module_scope, TValue<String> module_name,
        LanguageMode language_mode = LanguageMode::StandardsCompliant,
        ModuleResultMode result_mode = ModuleResultMode::File);

}  // namespace cl

#endif  // CL_CODEGEN_H
