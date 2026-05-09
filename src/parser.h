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

    struct TokenVector;

    enum class StartRule
    {
        File,
        Interactive,
        Eval,
        FuncType,
        FString
    };

    class ParseError : public std::runtime_error
    {
    public:
        ParseError(std::string message, bool _incomplete_input = false,
                   uint32_t _next_indentation_level = 0)
            : std::runtime_error(std::move(message)),
              incomplete_input_(_incomplete_input),
              next_indentation_level_(_next_indentation_level)
        {
        }

        bool incomplete_input() const { return incomplete_input_; }
        uint32_t next_indentation_level() const
        {
            return next_indentation_level_;
        }

    private:
        bool incomplete_input_;
        uint32_t next_indentation_level_;
    };

    AstVector parse(VirtualMachine &vm, const TokenVector &tv,
                    StartRule start_rule);

}  // namespace cl

#endif  // CL_PARSER_H
