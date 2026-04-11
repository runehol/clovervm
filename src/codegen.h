#ifndef CL_CODEGEN_H
#define CL_CODEGEN_H

#include "ast.h"
#include "code_object.h"
namespace cl
{
    struct AstVector;

    CodeObject *generate_code(const AstVector &av);

}  // namespace cl

#endif  // CL_CODEGEN_H
