#ifndef CL_PARSER_H
#define CL_PARSER_H

#include "ast.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

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

    class ParseError : public std::runtime_error
    {
    public:
        explicit ParseError(std::string message)
            : std::runtime_error(std::move(message))
        {
        }
    };

    AstVector
    parse(VirtualMachine &vm, const TokenVector &tv, StartRule start_rule,
          CompileContinuationInfo *compile_continuation_info = nullptr);

}  // namespace cl

#endif  // CL_PARSER_H
