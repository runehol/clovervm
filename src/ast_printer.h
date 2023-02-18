#include "ast.h"
#include <fmt/formatter.h>
#include <cassert>

#ifndef CL_AST_PRINTER_H
#define CL_AST_PRINTER_H

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
        case AstOperatorKind::NOP:
            return format_to(out, "");
        case AstOperatorKind::COMMA:
            return format_to(out, ", ");
        case AstOperatorKind::SUBSCRIPT:
            return format_to(out, ".");

        case AstOperatorKind::ADD:
            return format_to(out, " + ");
        case AstOperatorKind::SUBTRACT:
            return format_to(out, " - ");
        case AstOperatorKind::MULTIPLY:
            return format_to(out, " * ");
        case AstOperatorKind::DIVIDE:
            return format_to(out, " / ");
        case AstOperatorKind::INT_DIVIDE:
            return format_to(out, " // ");
        case AstOperatorKind::MATMULT:
            return format_to(out, " @ ");
        case AstOperatorKind::POWER:
            return format_to(out, " ** ");
        case AstOperatorKind::LEFTSHIFT:
            return format_to(out, " << ");
        case AstOperatorKind::RIGHTSHIFT:
            return format_to(out, " >> ");
        case AstOperatorKind::MODULO:
            return format_to(out, " % ");
        case AstOperatorKind::OR:
            return format_to(out, " | ");
        case AstOperatorKind::AND:
            return format_to(out, " & ");
        case AstOperatorKind::XOR:
            return format_to(out, " ^ ");

        case AstOperatorKind::NOT:
            return format_to(out, "not ");
        case AstOperatorKind::NEGATE:
            return format_to(out, "-");
        case AstOperatorKind::PLUS:
            return format_to(out, "+");
        case AstOperatorKind::INVERT:
            return format_to(out, "^");

        case AstOperatorKind::NONE:
            return format_to(out, "None");
        case AstOperatorKind::TRUE:
            return format_to(out, "True");
        case AstOperatorKind::FALSE:
            return format_to(out, "False");

        case AstOperatorKind::NUMBER:
        case AstOperatorKind::STRING:
            return format_to(out, "");


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

    template <typename FormatContext>
    auto format(const cl::AstVector &tv, FormatContext& ctx, int32_t node_idx, uint32_t indent, uint32_t precedence) -> decltype(ctx.out())
    {
        auto&& out = ctx.out();
        assert(node_idx >= 0);
        AstKind kind = tv.kinds[node_idx];
        switch(kind.node_kind)
        {
        case AstNodeKind::EXPRESSION_ASSIGN:
            return format_to(out, "{} {}= {}"
        EXPRESSION_BINARY,
        EXPRESSION_UNARY,
        EXPRESSION_LITERAL,

        }
    }


    template <typename FormatContext>
    auto format(const cl::AstVector &tv, FormatContext& ctx) -> decltype(ctx.out())
    {
        return format(tv, ctx, tv.root_node, 0, 0);
    }
};


#endif //CL_AST_PRINTER_H
