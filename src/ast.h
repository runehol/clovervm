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
        STATEMENT_SEQUENCE,
        EXPRESSION_ASSIGN,
        EXPRESSION_BINARY,
        STATEMENT_UNARY,
        EXPRESSION_NUMBER_LITERAL,
        EXPRESSION_STRING_LITERAL,

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
        AstChildren(int32_t _child0=-1, int32_t _child1=-1):
            child0(_child0),
            child1(_child1)
        {}

        int32_t child0;
        int32_t child1;
    };

    struct AstVector
    {
        std::vector<AstKind> kinds;
        std::vector<uint32_t> source_offsets;
        std::vector<AstChildren> children01;
        std::vector<int32_t> children2;

        int32_t root_node = -1;

        size_t size() const {
            assert(kinds.size() == source_offsets.size());
            assert(kinds.size() == children01.size());
            assert(kinds.size() == children2.size());
            return kinds.size();
        }

        int32_t emplace_back(AstKind kind, uint32_t source_offset, int32_t child0=-1, int32_t child1=-1, int32_t child2=-1)
        {
            int32_t idx = size();
            kinds.push_back(kind);
            source_offsets.push_back(source_offset);
            children01.emplace_back(child0, child1);
            children2.emplace_back(child2);
            return idx;
        }
    };

}

#endif //CL_AST_H
