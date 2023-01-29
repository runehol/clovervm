#ifndef CL_AST_H
#define CL_AST_H

#include <vector>
#include <cstdint>
#include <cassert>
#include <iosfwd>

namespace cl
{

    enum class AstNodeKind : uint8_t
    {
        STATEMENT_FUNCTION_DEF,
        STATEMENT_IF,
        STATEMENT_CLASS_DEF,
        STATEMENT_WITH,
        STATEMENT_FOR,
        STATEMENT_TRY,
        STATEMENT_WHILE,
        STATEMENT_MATCH,
        STATEMENT_RETURN,
        STATEMENT_IMPORT,
        STATEMENT_RAISE,
        STATEMENT_PASS,
        STATEMENT_DEL,
        STATEMENT_YIELD,
        STATEMENT_ASSERT,
        STATEMENT_BREAK,
        STATEMENT_CONTINUE,
        STATEMENT_GLOBAL,
        STATEMENT_NON_LOCAL,
        EXPRESSION_ASSIGN,
        EXPRESSION_BINARY,
        STATEMENT_UNARY
    };


    enum class AstOperatorKind : uint8_t
    {
        COMMA,
        DOT,
        SUBSCRIPT,

        NONE,
        ADD,
        SUB,
        MULTIPLY,
        DIVIDE,
        INT_DIVIDE,
        POWER,
        LEFTSHIFT,
        RIGHTSHIFT,
        MODULO,
        OR,
        AND,
        XOR,

        NOT,
        NEGATE


    };

    const char *to_string(AstNodeKind t);
    std::ostream &operator<<(std::ostream &o, AstNodeKind t);
    const char *to_string(AstOperatorKind t);
    std::ostream &operator<<(std::ostream &o, AstOperatorKind t);


    struct AstKind
    {
        AstNodeKind node_kind;
        AstOperatorKind operator_kind;
    };

    struct AstChildren
    {
        AstChildren(int32_t _lhs=-1, int32_t _rhs=-1):
            lhs(_lhs),
            rhs(_rhs)
        {}

        int32_t lhs;
        int32_t rhs;
    };

    struct AstVector
    {
        std::vector<AstKind> kinds;
        std::vector<uint32_t> source_offsets;
        std::vector<AstChildren> children;

        size_t size() const {
            assert(kinds.size() == source_offsets.size());
            assert(kinds.size() == children.size());
            return kinds.size();
        }

        void emplace_back(AstKind kind, uint32_t source_offset, int32_t lhs=-1, int32_t rhs=-1)
        {
            kinds.push_back(kind);
            source_offsets.push_back(source_offset);
            children.emplace_back(lhs, rhs);
        }
    };

}

#endif //CL_AST_H
