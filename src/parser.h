#ifndef CL_PARSER_H
#define CL_PARSER_H

#include "ast.h"
#include "typed_value.h"
#include <cstdint>

namespace cl
{

    class VirtualMachine;

    class TokenVector;

    enum class StartRule
    {
        File,
        Interactive,
        Eval,
        FuncType,
        FString
    };

    struct CompileContinuationInfo
    {
        bool incomplete_input = false;
        uint32_t next_indentation_level = 0;
    };

    Expected<AstVector>
    parse(VirtualMachine &vm, const TokenVector &tv, StartRule start_rule,
          CompileContinuationInfo *compile_continuation_info = nullptr);

}  // namespace cl

#endif  // CL_PARSER_H
