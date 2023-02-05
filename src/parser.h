#ifndef CL_PARSER_H
#define CL_PARSER_H

#include "ast.h"

namespace cl
{

    struct TokenVector;

    enum class StartRule
    {
        File,
        Interactive,
        Eval,
        FuncType,
        FString
    };


    AstVector parse(const TokenVector &t, StartRule start_rule);


}

#endif //CL_PARSER_H
