#ifndef CL_AST_H
#define CL_AST_H

#include <vector>
#include <cstdint>
#include <cassert>
#include <iosfwd>
#include "cl_value.h"
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

    constexpr bool is_statement(AstNodeKind k)
    {
        switch(k)
        {
		case AstNodeKind::STATEMENT_FUNCTION_DEF:
		case AstNodeKind::STATEMENT_IF:
		case AstNodeKind::STATEMENT_CLASS_DEF:
		case AstNodeKind::STATEMENT_WITH:
		case AstNodeKind::STATEMENT_FOR:
		case AstNodeKind::STATEMENT_TRY:
		case AstNodeKind::STATEMENT_WHILE:
		case AstNodeKind::STATEMENT_MATCH:
		case AstNodeKind::STATEMENT_RETURN:
		case AstNodeKind::STATEMENT_IMPORT:
		case AstNodeKind::STATEMENT_RAISE:
		case AstNodeKind::STATEMENT_PASS:
		case AstNodeKind::STATEMENT_DEL:
		case AstNodeKind::STATEMENT_YIELD:
		case AstNodeKind::STATEMENT_ASSERT:
		case AstNodeKind::STATEMENT_BREAK:
		case AstNodeKind::STATEMENT_CONTINUE:
		case AstNodeKind::STATEMENT_GLOBAL:
		case AstNodeKind::STATEMENT_NON_LOCAL:
		case AstNodeKind::STATEMENT_SEQUENCE:
            return true;
		case AstNodeKind::EXPRESSION_TUPLE:
		case AstNodeKind::EXPRESSION_LIST:
		case AstNodeKind::EXPRESSION_ASSIGN:
		case AstNodeKind::EXPRESSION_BINARY:
		case AstNodeKind::EXPRESSION_UNARY:
		case AstNodeKind::EXPRESSION_LITERAL:
            return false;
		case AstNodeKind::SEQUENCE:
            return false;
        }
    }

    constexpr bool is_expression(AstNodeKind k)
    {
        switch(k)
        {
		case AstNodeKind::STATEMENT_FUNCTION_DEF:
		case AstNodeKind::STATEMENT_IF:
		case AstNodeKind::STATEMENT_CLASS_DEF:
		case AstNodeKind::STATEMENT_WITH:
		case AstNodeKind::STATEMENT_FOR:
		case AstNodeKind::STATEMENT_TRY:
		case AstNodeKind::STATEMENT_WHILE:
		case AstNodeKind::STATEMENT_MATCH:
		case AstNodeKind::STATEMENT_RETURN:
		case AstNodeKind::STATEMENT_IMPORT:
		case AstNodeKind::STATEMENT_RAISE:
		case AstNodeKind::STATEMENT_PASS:
		case AstNodeKind::STATEMENT_DEL:
		case AstNodeKind::STATEMENT_YIELD:
		case AstNodeKind::STATEMENT_ASSERT:
		case AstNodeKind::STATEMENT_BREAK:
		case AstNodeKind::STATEMENT_CONTINUE:
		case AstNodeKind::STATEMENT_GLOBAL:
		case AstNodeKind::STATEMENT_NON_LOCAL:
		case AstNodeKind::STATEMENT_SEQUENCE:
            return false;
		case AstNodeKind::EXPRESSION_TUPLE:
		case AstNodeKind::EXPRESSION_LIST:
		case AstNodeKind::EXPRESSION_ASSIGN:
		case AstNodeKind::EXPRESSION_BINARY:
		case AstNodeKind::EXPRESSION_UNARY:
		case AstNodeKind::EXPRESSION_LITERAL:
            return true;
		case AstNodeKind::SEQUENCE:
            return false;
        }
    }


    enum class AstOperatorKind : uint8_t
    {
        NOP,
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
        BITWISE_OR,
        BITWISE_AND,
        BITWISE_XOR,

        NOT,
        NEGATE,
        PLUS,
        BITWISE_NOT,


        //literal expressions
        NUMBER,
        STRING,
        NONE,
        TRUE,
        FALSE,

    };
    constexpr static size_t AstOperatorKindSize = size_t(AstOperatorKind::FALSE)+1;


    const char *to_string(AstNodeKind t);
    std::ostream &operator<<(std::ostream &o, AstNodeKind t);
    const char *to_string(AstOperatorKind t);
    std::ostream &operator<<(std::ostream &o, AstOperatorKind t);


    struct AstKind
    {
        constexpr AstKind(AstNodeKind _node_kind, AstOperatorKind _operator_kind=AstOperatorKind::NOP)
            : node_kind(_node_kind), operator_kind(_operator_kind)
        {}

        AstNodeKind node_kind;
        AstOperatorKind operator_kind;
    };


    enum class ExpressionPrecedence : uint32_t
    {
        Lowest,
        Disjunction,
        Conjunction,
        Inversion,
        Comparison,
        BitwiseOr,
        BitwiseXor,
        BitwiseAnd,
        Shift,
        Sum,
        Term,
        Factor,
        Power,
        Await,
        Primary,
        Atom
    };

    constexpr ExpressionPrecedence next(ExpressionPrecedence p) { return ExpressionPrecedence(uint32_t(p) + 1); }

    constexpr ExpressionPrecedence precedence_for_kind(AstKind k)
    {
        switch(k.operator_kind)
        {
		case AstOperatorKind::NOP:
            return ExpressionPrecedence::Lowest;

		case AstOperatorKind::NOT:
            return ExpressionPrecedence::Inversion;

		case AstOperatorKind::BITWISE_OR:
            return ExpressionPrecedence::BitwiseOr;
		case AstOperatorKind::BITWISE_AND:
            return ExpressionPrecedence::BitwiseAnd;
		case AstOperatorKind::BITWISE_XOR:
            return ExpressionPrecedence::BitwiseXor;

		case AstOperatorKind::LEFTSHIFT:
		case AstOperatorKind::RIGHTSHIFT:
            return ExpressionPrecedence::Shift;

		case AstOperatorKind::ADD:
		case AstOperatorKind::SUBTRACT:
            return ExpressionPrecedence::Sum;

		case AstOperatorKind::MULTIPLY:
		case AstOperatorKind::DIVIDE:
		case AstOperatorKind::INT_DIVIDE:
		case AstOperatorKind::MATMULT:
		case AstOperatorKind::MODULO:
            return ExpressionPrecedence::Term;

		case AstOperatorKind::NEGATE:
		case AstOperatorKind::PLUS:
		case AstOperatorKind::BITWISE_NOT:
            return ExpressionPrecedence::Factor;

		case AstOperatorKind::POWER:
            return ExpressionPrecedence::Power;


		case AstOperatorKind::DOT:
		case AstOperatorKind::SUBSCRIPT:
            return ExpressionPrecedence::Primary;

		case AstOperatorKind::NUMBER:
		case AstOperatorKind::STRING:
		case AstOperatorKind::NONE:
		case AstOperatorKind::TRUE:
		case AstOperatorKind::FALSE:
            return ExpressionPrecedence::Atom;
            }

    }


    struct AstChildren
    {
        constexpr AstChildren(int32_t _lhs=-1, int32_t _rhs=-1):
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
        std::vector<CLValue> constants;

        int32_t root_node = -1;

        size_t size() const {
            assert(kinds.size() == source_offsets.size());
            assert(kinds.size() == children.size());
            assert(kinds.size() == constants.size());
            return kinds.size();
        }

        int32_t emplace_back(AstKind kind, uint32_t source_offset, int32_t lhs=-1, int32_t rhs=-1, CLValue constant = cl_None)
        {
            int32_t idx = size();
            kinds.push_back(kind);
            source_offsets.push_back(source_offset);
            children.emplace_back(lhs, rhs);
            constants.emplace_back(constant);
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
