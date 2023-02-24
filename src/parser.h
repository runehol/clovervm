#ifndef CL_PARSER_H
#define CL_PARSER_H

#include "ast.h"

namespace cl
{

    class VirtualMachine;

    struct TokenVector;

    enum class StartRule
    {
        File,
        Interactive,
        Eval,
        FuncType,
        FString
    };


    AstVector parse(VirtualMachine &vm, const TokenVector &tv, StartRule start_rule);


}

#endif //CL_PARSER_H
