#ifndef CL_CODEGEN_H
#define CL_CODEGEN_H

#include "code_object.h"
#include "ast.h"
namespace cl
{
    struct AstVector;

    CodeObject generate_code(const AstVector &av);


}

#endif //CL_CODEGEN_H
