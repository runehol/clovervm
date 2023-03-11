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
            return format_to(out, "^");


        case cl::AstOperatorKind::EQUAL:
            return format_to(out, "==");
        case cl::AstOperatorKind::NOT_EQUAL:
            return format_to(out, "!=");
        case cl::AstOperatorKind::LESS:
            return format_to(out, "<");
        case cl::AstOperatorKind::LESS_EQUAL:
            return format_to(out, "<=");
        case cl::AstOperatorKind::GREATER:
            return format_to(out, ">");
        case cl::AstOperatorKind::GREATER_EQUAL:
            return format_to(out, ">=");
        case cl::AstOperatorKind::IS:
            return format_to(out, "is");
        case cl::AstOperatorKind::IS_NOT:
            return format_to(out, "is not");
        case cl::AstOperatorKind::IN:
            return format_to(out, "in");
        case cl::AstOperatorKind::NOT_IN:
            return format_to(out, "not in");

        case cl::AstOperatorKind::SHORTCUTTING_AND:
            return format_to(out, "and");

        case cl::AstOperatorKind::SHORTCUTTING_OR:
            return format_to(out, "or");


        case cl::AstOperatorKind::NOT:
            return format_to(out, "not");
        case cl::AstOperatorKind::NEGATE:
            return format_to(out, "-");
        case cl::AstOperatorKind::PLUS:
            return format_to(out, "+");
        case cl::AstOperatorKind::BITWISE_NOT:
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
    void emit_indent(Out &out, uint32_t indent)
    {
        for(uint32_t i = 0; i < indent; ++i)
        {
            format_to(out, "    ");
        }
    }

    template <typename Out>
    void render_node(const cl::AstVector &av, Out &out, int32_t node_idx, uint32_t indent, cl::ExpressionPrecedence outer_precedence)
    {
        assert(node_idx >= 0);
        cl::AstKind kind = av.kinds[node_idx];
        cl::ExpressionPrecedence self_precedence = precedence_for_kind(kind);
        if(is_expression(kind.node_kind) && self_precedence <= outer_precedence)
        {
            format_to(out, "(");
        }

        cl::AstChildren children = av.children[node_idx];
        switch(kind.node_kind)
        {
        case cl::AstNodeKind::STATEMENT_ASSIGN:
            emit_indent(out, indent);
            render_node(av, out, children[0], indent, self_precedence);
            format_to(out, " {}= ", kind.operator_kind);
            render_node(av, out, children[1], indent, self_precedence);
            format_to(out, "\n");
            break;
        case cl::AstNodeKind::EXPRESSION_ASSIGN:
            render_node(av, out, children[0], indent, self_precedence);
            assert(kind.operator_kind == cl::AstOperatorKind::NOP);
            format_to(out, " := ", kind.operator_kind);
            render_node(av, out, children[1], indent, self_precedence);
            break;

        case cl::AstNodeKind::EXPRESSION_BINARY:
        case cl::AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY:
            render_node(av, out, children[0], indent, self_precedence);
            format_to(out, " {} ", kind.operator_kind);
            render_node(av, out, children[1], indent, self_precedence);
            break;
        case cl::AstNodeKind::EXPRESSION_UNARY:
            format_to(out, "{} ", kind.operator_kind);
            render_node(av, out, children[0], indent, self_precedence);
            break;
        case cl::AstNodeKind::EXPRESSION_LITERAL:
            if(kind.operator_kind == cl::AstOperatorKind::NUMBER)
            {
                format_to(out, L"{}", string_for_number_token(*av.compilation_unit, av.source_offsets[node_idx]));
            } else if(kind.operator_kind == cl::AstOperatorKind::STRING)
            {
                format_to(out, L" {}", string_for_string_token(*av.compilation_unit, av.source_offsets[node_idx]));
            }
            break;
        case cl::AstNodeKind::EXPRESSION_VARIABLE_REFERENCE:
            format_to(out, L"{}", string_as_wchar_t(av.constants[node_idx]));
            break;

        case cl::AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT:
            throw std::runtime_error("should be handled elsewhere");

        case cl::AstNodeKind::EXPRESSION_COMPARISON:
            render_node(av, out, children[0], indent, self_precedence);
            for(size_t i = 1; i < children.size(); ++i)
            {
                uint32_t ch = children[i];
                cl::AstKind ch_kind = av.kinds[ch];
                uint32_t ch_child = av.children[ch][0];
                format_to(out, " {} ", ch_kind.operator_kind);
                render_node(av, out, ch_child, indent, self_precedence);
            }
            break;

        case cl::AstNodeKind::EXPRESSION_LIST:
            format_to(out, "[");
            for(size_t i = 0; i < children.size(); ++i)
            {
                if(i != 0)
                {
                    format_to(out, ", ");
                }
                render_node(av, out, children[i], indent, cl::ExpressionPrecedence::Lowest);
            }
            format_to(out, "]");
            break;

        case cl::AstNodeKind::EXPRESSION_TUPLE:
            format_to(out, "(");
            for(size_t i = 0; i < children.size(); ++i)
            {
                if(i != 0)
                {
                    format_to(out, ", ");
                }
                render_node(av, out, children[i], indent, cl::ExpressionPrecedence::Lowest);
            }
            if(children.size() == 1)
            {
                format_to(out, ",");
            }
            format_to(out, ")");
            break;

        case cl::AstNodeKind::EXPRESSION_CALL:
            render_node(av, out, children[0], indent, self_precedence);
            render_node(av, out, children[1], indent, self_precedence);
            break;


        case cl::AstNodeKind::PARAMETER_SEQUENCE:
            format_to(out, "(");
            for(size_t i = 0; i < children.size(); ++i)
            {
                if(i != 0)
                {
                    format_to(out, ", ");
                }
                render_node(av, out, children[i], indent, cl::ExpressionPrecedence::Lowest);
            }
            format_to(out, ")");
            break;


        case cl::AstNodeKind::STATEMENT_FUNCTION_DEF:
            emit_indent(out, indent);
            format_to(out, L"def {}", string_as_wchar_t(av.constants[node_idx]));

            render_node(av, out, children[0], indent, cl::ExpressionPrecedence::Lowest);
            format_to(out, ":\n");
            render_node(av, out, children[1], indent+1, cl::ExpressionPrecedence::Lowest);
            break;

        case cl::AstNodeKind::STATEMENT_EXPRESSION:
            emit_indent(out, indent);
            render_node(av, out, children[0], indent, cl::ExpressionPrecedence::Lowest);
            format_to(out, "\n");
            break;
        case cl::AstNodeKind::STATEMENT_IF:
            emit_indent(out, indent);
            format_to(out, "if ");
            render_node(av, out, children[0], indent, cl::ExpressionPrecedence::Lowest);
            format_to(out, ":\n");
            render_node(av, out, children[1], indent+1, cl::ExpressionPrecedence::Lowest);

            for(size_t child_offset = 2; child_offset < children.size(); child_offset += 2)
            {
                format_to(out, "elif ");
                render_node(av, out, children[child_offset+0], indent, cl::ExpressionPrecedence::Lowest);
                format_to(out, ":\n");
                render_node(av, out, children[child_offset+1], indent+1, cl::ExpressionPrecedence::Lowest);

            }
            if(children.size() & 1) // if odd, we have an else block
            {
                emit_indent(out, indent);
                format_to(out, "else:\n");
                render_node(av, out, children.back(), indent+1, cl::ExpressionPrecedence::Lowest);
            }
            break;

        case cl::AstNodeKind::STATEMENT_WHILE:
            emit_indent(out, indent);
            format_to(out, "while ");
            render_node(av, out, children[0], indent, cl::ExpressionPrecedence::Lowest);
            format_to(out, ":\n");
            render_node(av, out, children[1], indent+1, cl::ExpressionPrecedence::Lowest);
            if(children.size() == 3)
            {
                emit_indent(out, indent);
                format_to(out, "else:\n");
                render_node(av, out, children[2], indent+1, cl::ExpressionPrecedence::Lowest);
            }
            break;

        case cl::AstNodeKind::STATEMENT_RETURN:
            emit_indent(out, indent);
            format_to(out, "return");
            if(children.size() > 0)
            {
                format_to(out, " ");
                render_node(av, out, children[0], indent, cl::ExpressionPrecedence::Lowest);
            }
            format_to(out, "\n");
            break;

        case cl::AstNodeKind::STATEMENT_PASS:
            emit_indent(out, indent);
            format_to(out, "pass\n");
            break;
        case cl::AstNodeKind::STATEMENT_BREAK:
            emit_indent(out, indent);
            format_to(out, "break\n");
            break;
        case cl::AstNodeKind::STATEMENT_CONTINUE:
            emit_indent(out, indent);
            format_to(out, "continue\n");
            break;

        case cl::AstNodeKind::STATEMENT_NONLOCAL:
        case cl::AstNodeKind::STATEMENT_GLOBAL:
            emit_indent(out, indent);
            if(kind.node_kind == cl::AstNodeKind::STATEMENT_NONLOCAL)
            {
                format_to(out, "nonlocal ");
            } else {
                format_to(out, "global ");
            }
            for(size_t i = 0; i < children.size(); ++i)
            {
                if(i != 0)
                {
                    format_to(out, ", ");
                }
                render_node(av, out, children[i], indent, cl::ExpressionPrecedence::Lowest);
            }
            format_to(out, "nonlocal\n");
            break;

        case cl::AstNodeKind::STATEMENT_SEQUENCE:
            for(size_t i = 0; i < children.size(); ++i)
            {
                render_node(av, out, children[i], indent, cl::ExpressionPrecedence::Lowest);
            }
            break;


        }

        if(is_expression(kind.node_kind) && self_precedence <= outer_precedence)
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
