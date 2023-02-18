#ifndef CL_AST_H
#define CL_AST_H

#include <vector>
#include <cstdint>
#include <cassert>
#include <iosfwd>
#include <absl/container/inlined_vector.h>

namespace cl
{
    struct CompilationUnit;

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
        EXPRESSION_TUPLE,
        EXPRESSION_LIST,
        EXPRESSION_ASSIGN,
        EXPRESSION_BINARY,
        EXPRESSION_UNARY,
        EXPRESSION_LITERAL,
        SEQUENCE,

    };


    enum class AstOperatorKind : uint8_t
    {
        NOP,
        COMMA,
        DOT,
        SUBSCRIPT,

        ADD,
        SUBTRACT,
        MULTIPLY,
        DIVIDE,
        INT_DIVIDE,
        MATMULT,
        POWER,
        LEFTSHIFT,
        RIGHTSHIFT,
        MODULO,
        OR,
        AND,
        XOR,

        NOT,
        NEGATE,
        PLUS,
        INVERT,


        //literal expressions
        NUMBER,
        STRING,
        NONE,
        TRUE,
        FALSE,

    };

    const char *to_string(AstNodeKind t);
    std::ostream &operator<<(std::ostream &o, AstNodeKind t);
    const char *to_string(AstOperatorKind t);
    std::ostream &operator<<(std::ostream &o, AstOperatorKind t);


    struct AstKind
    {
        AstKind(AstNodeKind _node_kind, AstOperatorKind _operator_kind=AstOperatorKind::NOP)
            : node_kind(_node_kind), operator_kind(_operator_kind)
        {}

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
        AstVector(const CompilationUnit *_compilation_unit)
            : compilation_unit(_compilation_unit)
        {}

        const CompilationUnit *compilation_unit;
        std::vector<AstKind> kinds;
        std::vector<uint32_t> source_offsets;
        std::vector<AstChildren> children;

        int32_t root_node = -1;

        size_t size() const {
            assert(kinds.size() == source_offsets.size());
            assert(kinds.size() == children.size());
            return kinds.size();
        }

        int32_t emplace_back(AstKind kind, uint32_t source_offset, int32_t lhs=-1, int32_t rhs=-1)
        {
            int32_t idx = size();
            kinds.push_back(kind);
            source_offsets.push_back(source_offset);
            children.emplace_back(lhs, rhs);
            return idx;
        }

        absl::InlinedVector<int32_t, 4> extract_child_sequence(int32_t parent_idx)
        {
            absl::InlinedVector<int32_t, 4> children;
            while(true)
            {
                AstChildren ch = children[parent_idx];
                if(ch.lhs != -1)
                {
                    assert(kinds[ch.lhs].node_kind != AstNodeKind::SEQUENCE);
                    children.push_back(ch.lhs);
                }

                if(ch.rhs == -1)
                {
                    break;
                } else if(kinds[ch.rhs].node_kind != AstNodeKind::SEQUENCE)
                {
                    children.push_back(ch.rhs);
                    break;
                } else {
                    parent_idx = ch.rhs;
                }
            }

            return children;
        }


    };



}

#endif //CL_AST_H
