#ifndef CL_AST_PRINT_H
#define CL_AST_PRINT_H

#include "ast.h"
#include <fmt/format.h>
#include <cassert>


template<>
struct fmt::formatter<cl::AstOperatorKind>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(const cl::AstOperatorKind ok, FormatContext& ctx) -> decltype(ctx.out())
    {
        auto&& out = ctx.out();
        switch(ok)
        {
        case cl::AstOperatorKind::NOP:
            return format_to(out, "");
        case cl::AstOperatorKind::DOT:
            return format_to(out, ".");
        case cl::AstOperatorKind::SUBSCRIPT:
            return format_to(out, "[");

        case cl::AstOperatorKind::ADD:
            return format_to(out, "+");
        case cl::AstOperatorKind::SUBTRACT:
            return format_to(out, "-");
        case cl::AstOperatorKind::MULTIPLY:
            return format_to(out, "*");
        case cl::AstOperatorKind::DIVIDE:
            return format_to(out, "/");
        case cl::AstOperatorKind::INT_DIVIDE:
            return format_to(out, "//");
        case cl::AstOperatorKind::MATMULT:
            return format_to(out, "@");
        case cl::AstOperatorKind::POWER:
            return format_to(out, "**");
        case cl::AstOperatorKind::LEFTSHIFT:
            return format_to(out, "<<");
        case cl::AstOperatorKind::RIGHTSHIFT:
            return format_to(out, ">>");
        case cl::AstOperatorKind::MODULO:
            return format_to(out, "%");
        case cl::AstOperatorKind::BITWISE_OR:
            return format_to(out, "|");
        case cl::AstOperatorKind::BITWISE_AND:
            return format_to(out, "&");
        case cl::AstOperatorKind::BITWISE_XOR:
            return format_to(out, " ^ ");

        case cl::AstOperatorKind::NOT:
            return format_to(out, "not");
        case cl::AstOperatorKind::NEGATE:
            return format_to(out, "-");
        case cl::AstOperatorKind::PLUS:
            return format_to(out, "+");
        case cl::AstOperatorKind::INVERT:
            return format_to(out, "^");

        case cl::AstOperatorKind::NONE:
            return format_to(out, "None");
        case cl::AstOperatorKind::TRUE:
            return format_to(out, "True");
        case cl::AstOperatorKind::FALSE:
            return format_to(out, "False");

        case cl::AstOperatorKind::NUMBER:
        case cl::AstOperatorKind::STRING:
            return out;


        }
    }
};


template<>
struct fmt::formatter<cl::AstVector>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.end();
    }

    template <typename Out>
    void render_node(const cl::AstVector &av, Out &out, int32_t node_idx, uint32_t indent, cl::ExpressionPrecedence outer_precedence)
    {
        assert(node_idx >= 0);
        cl::AstKind kind = av.kinds[node_idx];
        cl::ExpressionPrecedence self_precedence = precedence_for_kind(kind);
        if(is_statement(kind.node_kind))
        {
            for(uint32_t i = 0; i < indent; ++i)
            {
                format_to(out, "    ");
            }
        } else if(is_expression(kind.node_kind) && self_precedence <= outer_precedence)
        {
            format_to(out, "(");
        }

        cl::AstChildren children = av.children[node_idx];
        switch(kind.node_kind)
        {
        case cl::AstNodeKind::EXPRESSION_ASSIGN:
            render_node(av, out, children.lhs, indent, self_precedence);
            format_to(out, " {}= ", kind.operator_kind);
            render_node(av, out, children.rhs, indent, self_precedence);
            break;
        case cl::AstNodeKind::EXPRESSION_BINARY:
            render_node(av, out, children.lhs, indent, self_precedence);
            format_to(out, " {} ", kind.operator_kind);
            render_node(av, out, children.rhs, indent, self_precedence);
            break;
        case cl::AstNodeKind::EXPRESSION_UNARY:
            format_to(out, "{} ", kind.operator_kind);
            render_node(av, out, children.lhs, indent, self_precedence);
            break;
        case cl::AstNodeKind::EXPRESSION_LITERAL:
            format_to(out, "{}", kind.operator_kind);
            if(kind.operator_kind == cl::AstOperatorKind::NUMBER)
            {
                format_to(out, L"{}", string_for_number_token(*av.compilation_unit, av.source_offsets[node_idx]));
            } else if(kind.operator_kind == cl::AstOperatorKind::STRING)
            {
                format_to(out, L" {}", string_for_string_token(*av.compilation_unit, av.source_offsets[node_idx]));
            }
            break;
        default:
            format_to(out, "Unknown node kind!");
        }

        if(is_statement(kind.node_kind))
        {
            format_to(out, "\n");
        } else if(is_expression(kind.node_kind) && self_precedence <= outer_precedence)
        {
            format_to(out, ")");
        }


    }


    template <typename FormatContext>
    auto format(const cl::AstVector &tv, FormatContext& ctx) -> decltype(ctx.out())
    {
        auto&& out = ctx.out();
        render_node(tv, out, tv.root_node, 0, cl::ExpressionPrecedence::Lowest);
        return out;
    }
};


#endif //CL_AST_PRINT_H
