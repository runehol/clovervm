#ifndef CL_CODEGEN_H
#define CL_CODEGEN_H

#include "ast.h"
#include "code_object.h"
namespace cl
{
    struct AstVector;
    struct String;
    template <typename T> class TValue;

    CodeObject *codegen_module(const AstVector &av, TValue<String> module_name);

}  // namespace cl

#endif  // CL_CODEGEN_H
