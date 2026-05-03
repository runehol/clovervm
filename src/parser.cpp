#include "parser.h"

#include "ast.h"
#include "compilation_unit.h"
#include "token.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <algorithm>
#include <cstdint>
#include <string>

namespace cl
{
    static bool is_hex_digit(wchar_t c)
    {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ||
               (c >= L'A' && c <= L'F');
    }

    static int hex_value(wchar_t c)
    {
        if(c >= L'0' && c <= L'9')
        {
            return c - L'0';
        }
        if(c >= L'a' && c <= L'f')
        {
            return 10 + (c - L'a');
        }
        return 10 + (c - L'A');
    }

    static std::wstring decode_python_string_literal(std::wstring_view token)
    {
        size_t prefix_len = 0;
        bool is_raw = false;
        while(prefix_len < token.size())
        {
            wchar_t c = token[prefix_len];
            if(c == L'r' || c == L'R')
            {
                is_raw = true;
                ++prefix_len;
            }
            else if(c == L'u' || c == L'U')
            {
                ++prefix_len;
            }
            else
            {
                break;
            }
        }

        if(prefix_len >= token.size())
        {
            throw std::runtime_error("Invalid string literal");
        }

        wchar_t quote = token[prefix_len];
        if(quote != L'\'' && quote != L'"')
        {
            throw std::runtime_error("Invalid string literal");
        }
        if(token.size() < prefix_len + 2 || token.back() != quote)
        {
            throw std::runtime_error("Invalid string literal");
        }

        std::wstring body = std::wstring(
            token.substr(prefix_len + 1, token.size() - prefix_len - 2));
        if(is_raw)
        {
            return body;
        }

        std::wstring out;
        out.reserve(body.size());
        for(size_t i = 0; i < body.size(); ++i)
        {
            wchar_t c = body[i];
            if(c != L'\\')
            {
                out.push_back(c);
                continue;
            }

            if(i + 1 >= body.size())
            {
                throw std::runtime_error("Invalid escape in string literal");
            }

            wchar_t esc = body[++i];
            switch(esc)
            {
                case L'\n':
                    break;
                case L'\\':
                    out.push_back(L'\\');
                    break;
                case L'\'':
                    out.push_back(L'\'');
                    break;
                case L'"':
                    out.push_back(L'"');
                    break;
                case L'a':
                    out.push_back(L'\a');
                    break;
                case L'b':
                    out.push_back(L'\b');
                    break;
                case L'f':
                    out.push_back(L'\f');
                    break;
                case L'n':
                    out.push_back(L'\n');
                    break;
                case L'r':
                    out.push_back(L'\r');
                    break;
                case L't':
                    out.push_back(L'\t');
                    break;
                case L'v':
                    out.push_back(L'\v');
                    break;
                case L'x':
                    {
                        if(i + 2 >= body.size() || !is_hex_digit(body[i + 1]) ||
                           !is_hex_digit(body[i + 2]))
                        {
                            throw std::runtime_error(
                                "Invalid \\x escape in string literal");
                        }
                        int value = (hex_value(body[i + 1]) << 4) +
                                    hex_value(body[i + 2]);
                        out.push_back(static_cast<wchar_t>(value));
                        i += 2;
                        break;
                    }
                case L'u':
                case L'U':
                    {
                        size_t digits = esc == L'u' ? 4 : 8;
                        if(i + digits >= body.size())
                        {
                            throw std::runtime_error(
                                "Invalid unicode escape in string literal");
                        }
                        uint32_t value = 0;
                        for(size_t j = 1; j <= digits; ++j)
                        {
                            wchar_t h = body[i + j];
                            if(!is_hex_digit(h))
                            {
                                throw std::runtime_error(
                                    "Invalid unicode escape in string literal");
                            }
                            value = (value << 4) + hex_value(h);
                        }
                        out.push_back(static_cast<wchar_t>(value));
                        i += digits;
                        break;
                    }
                default:
                    if(esc >= L'0' && esc <= L'7')
                    {
                        int value = esc - L'0';
                        size_t consumed = 0;
                        while(consumed < 2 && i + 1 < body.size() &&
                              body[i + 1] >= L'0' && body[i + 1] <= L'7')
                        {
                            value = value * 8 + (body[i + 1] - L'0');
                            ++i;
                            ++consumed;
                        }
                        out.push_back(static_cast<wchar_t>(value & 0xFF));
                    }
                    else
                    {
                        out.push_back(L'\\');
                        out.push_back(esc);
                    }
                    break;
            }
        }
        return out;
    }

    class Parser
    {
    public:
        Parser(VirtualMachine &_vm, const TokenVector &_token_vec)
            : vm(_vm), token_vec(_token_vec), ast(token_vec.compilation_unit)
        {
        }

        AstVector parse(StartRule start_rule)
        {
            switch(start_rule)
            {
                case StartRule::File:
                    ast.root_node = file();
                    break;
                case StartRule::Interactive:
                    break;
                case StartRule::Eval:
                    ast.root_node = eval();
                    break;
                case StartRule::FuncType:
                    break;
                case StartRule::FString:
                    break;
            }

            return std::move(ast);
        }

    private:
        VirtualMachine &vm;
        const TokenVector &token_vec;
        AstVector ast;
        size_t token_pos = 0;

        Token peek()
        {
            assert(token_pos < token_vec.size());
            return token_vec.tokens[token_pos];
        };

        Token peek2()
        {
            size_t pos2 = token_pos + 1;
            if(pos2 < token_vec.size())
                return token_vec.tokens[pos2];
            return Token::ENDMARKER;
        };

        Token advance()
        {
            assert(token_pos < token_vec.size());
            return token_vec.tokens[token_pos++];
        };

        uint32_t source_pos_and_advance()
        {
            assert(token_pos < token_vec.size());
            return token_vec.source_offsets[token_pos++];
        }

        void consume(Token expected)
        {
            uint32_t source_pos = source_pos_for_token();
            Token actual = advance();
            if(expected != actual)
            {
                throw std::runtime_error(std::string("Expected token ") +
                                         to_string(expected) + ", got " +
                                         to_string(actual) +
                                         format_error_context(source_pos));
            }
        }

        bool match(Token expected)
        {
            Token actual = peek();
            if(actual == expected)
            {
                ++token_pos;
                return true;
            }
            return false;
        }

        uint32_t source_pos_for_token()
        {
            return token_vec.source_offsets[token_pos];
        }

        uint32_t source_pos_for_previous_token()
        {
            return token_vec.source_offsets[token_pos - 1];
        }

        bool is_at_end() { return peek() == Token::ENDMARKER; }

        static std::string narrow(std::wstring_view s)
        {
            return std::string(s.begin(), s.end());
        }

        std::string format_error_context(uint32_t source_pos)
        {
            auto [line, column] =
                ast.compilation_unit->get_line_column(source_pos);
            std::wstring_view line_view =
                ast.compilation_unit->get_line_view(source_pos);
            std::string snippet = narrow(line_view);
            static constexpr size_t max_snippet_len = 40;
            if(snippet.size() > max_snippet_len)
            {
                snippet.resize(max_snippet_len);
                snippet += "...";
            }

            return std::string(" at offset ") + std::to_string(source_pos) +
                   " (line " + std::to_string(line) + ", column " +
                   std::to_string(column) + "), near \"" + snippet + "\"";
        }

        int32_t not_implemented(const char *construct_name)
        {
            throw std::runtime_error(
                std::string("Not implemented: ") + construct_name + " (token " +
                to_string(peek()) + ")" +
                format_error_context(source_pos_for_token()));
        }

        // now the parser itself

        // helper to build sequences
        AstChildren sequence_until_stop_token(int32_t initial,
                                              int32_t (Parser::*parse_member)(),
                                              Token separator, Token stop)
        {
            AstChildren children;
            children.push_back(initial);
            while(match(separator))
            {
                if(peek() == stop)
                    break;

                int32_t member = (this->*parse_member)();
                children.push_back(member);
            }
            return children;
        }

        int32_t block()
        {
            int32_t stmts = -1;
            if(match(Token::NEWLINE))
            {
                consume(Token::INDENT);
                stmts = statements();
                consume(Token::DEDENT);
            }
            else
            {
                stmts = simple_stmts();
            }
            return stmts;
        }
        // expressions

        int32_t genexp() { return expression(); }

        int32_t star_expressions()
        {
            uint32_t tuple_start_pos = source_pos_for_token();
            int32_t result = star_expression();
            if(peek() == Token::COMMA)
            {
                AstChildren children =
                    sequence_until_stop_token(result, &Parser::star_expression,
                                              Token::COMMA, Token::NEWLINE);
                return ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE,
                                        tuple_start_pos, children);
            }
            else
            {
                return result;
            }
        }

        int32_t star_expression() { return expression(); }

        int32_t expressions()
        {
            uint32_t tuple_start_pos = source_pos_for_token();
            int32_t result = expression();
            if(peek() == Token::COMMA)
            {
                AstChildren children = sequence_until_stop_token(
                    result, &Parser::expression, Token::COMMA, Token::NEWLINE);
                return ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE,
                                        tuple_start_pos, children);
            }
            else
            {
                return result;
            }
        }

        int32_t expression() { return disjunction(); }

        int32_t assignment_expression()
        {
            if(peek() != Token::NAME)
            {
                uint32_t source_pos = source_pos_for_token();
                throw std::runtime_error(std::string("Expected token ") +
                                         to_string(Token::NAME) + ", got " +
                                         to_string(peek()) +
                                         format_error_context(source_pos));
            }
            int32_t lhs = atom();  // smallest rule that just consumes a name
                                   // and makes a nice node for us
            int32_t source_pos = source_pos_for_token();
            consume(Token::COLONEQUAL);
            int32_t rhs = expression();
            return ast.emplace_back(
                AstKind(AstNodeKind::EXPRESSION_ASSIGN, AstOperatorKind::NOP),
                source_pos, lhs, rhs);
        }

        int32_t named_expression()
        {
            if(peek() == Token::NAME && peek2() == Token::COLONEQUAL)
            {
                return assignment_expression();
            }
            else
            {
                return expression();
            }
        }

        int32_t disjunction()
        {
            int32_t lhs = conjunction();
            if(peek() == Token::OR)
            {
                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = disjunction();
                return ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY,
                            AstOperatorKind::SHORTCUTTING_OR),
                    source_pos, lhs, rhs);
            }
            else
            {
                return lhs;
            }
        }

        int32_t conjunction()
        {
            int32_t lhs = inversion();
            if(peek() == Token::AND)
            {
                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = conjunction();
                return ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY,
                            AstOperatorKind::SHORTCUTTING_AND),
                    source_pos, lhs, rhs);
            }
            else
            {
                return lhs;
            }
        }

        int32_t inversion()
        {
            if(match(Token::NOT))
            {
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_UNARY,
                                                AstOperatorKind::NOT),
                                        source_pos_for_previous_token(),
                                        inversion());
            }
            return comparison();
        }

        int32_t comparison()
        {
            AstChildren ch;
            ch.push_back(bitwise_or());
            uint32_t source_pos = source_pos_for_token();
            while(1)
            {
                bool valid_lookahead = false;
                switch(peek())
                {
                    case Token::EQEQUAL:
                    case Token::NOTEQUAL:
                    case Token::LESS:
                    case Token::LESSEQUAL:
                    case Token::GREATEREQUAL:
                    case Token::GREATER:
                    case Token::IN:
                    case Token::IS:
                        valid_lookahead = true;
                        break;
                    case Token::NOT:
                        if(peek2() == Token::IN)
                        {
                            valid_lookahead = true;
                        }
                        break;
                    default:
                        break;
                }
                if(!valid_lookahead)
                    break;
                ch.push_back(comparison_fragment());
            }

            if(ch.size() == 1)
            {
                return ch[0];
            }
            else
            {
                return ast.emplace_back(AstNodeKind::EXPRESSION_COMPARISON,
                                        source_pos, ch);
            }
        }

        int32_t comparison_fragment()
        {
            AstOperatorKind ok = AstOperatorKind::NOP;
            uint32_t source_pos = source_pos_for_token();
            if(match(Token::EQEQUAL))
            {
                ok = AstOperatorKind::EQUAL;
            }
            else if(match(Token::NOTEQUAL))
            {
                ok = AstOperatorKind::NOT_EQUAL;
            }
            else if(match(Token::LESS))
            {
                ok = AstOperatorKind::LESS;
            }
            else if(match(Token::LESSEQUAL))
            {
                ok = AstOperatorKind::LESS_EQUAL;
            }
            else if(match(Token::GREATEREQUAL))
            {
                ok = AstOperatorKind::GREATER_EQUAL;
            }
            else if(match(Token::GREATER))
            {
                ok = AstOperatorKind::GREATER;
            }
            else if(match(Token::IS))
            {
                ok = AstOperatorKind::IS;
                if(match(Token::NOT))
                {
                    ok = AstOperatorKind::IS_NOT;
                }
            }
            else if(match(Token::IN))
            {
                ok = AstOperatorKind::IN;
            }
            else if(peek() == Token::NOT && peek2() == Token::IN)
            {
                consume(Token::NOT);
                consume(Token::IN);
                ok = AstOperatorKind::NOT_IN;
            }
            assert(ok != AstOperatorKind::NOP);
            int32_t ch = bitwise_or();
            return ast.emplace_back(
                AstKind(AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT, ok),
                source_pos, ch);
        }

        int32_t bitwise_or()
        {
            int32_t result = bitwise_xor();
            while(match(Token::VBAR))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = bitwise_xor();
                result =
                    ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY,
                                             AstOperatorKind::BITWISE_OR),
                                     source_pos, result, rhs);
            }
            return result;
        }

        int32_t bitwise_xor()
        {
            int32_t result = bitwise_and();
            while(match(Token::CIRCUMFLEX))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = bitwise_and();
                result =
                    ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY,
                                             AstOperatorKind::BITWISE_XOR),
                                     source_pos, result, rhs);
            }
            return result;
        }

        int32_t bitwise_and()
        {
            int32_t result = shift_expr();
            while(match(Token::AMPER))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = shift_expr();
                result =
                    ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY,
                                             AstOperatorKind::BITWISE_AND),
                                     source_pos, result, rhs);
            }
            return result;
        }

        int32_t shift_expr()
        {
            int32_t result = sum();
            while(true)
            {
                AstOperatorKind op_kind = AstOperatorKind::NOP;
                switch(peek())
                {
                    case Token::LEFTSHIFT:
                        op_kind = AstOperatorKind::LEFTSHIFT;
                        break;
                    case Token::RIGHTSHIFT:
                        op_kind = AstOperatorKind::RIGHTSHIFT;
                        break;

                    default:
                        return result;
                }

                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = sum();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);
            }
        }

        int32_t sum()
        {
            int32_t result = term();
            while(true)
            {
                AstOperatorKind op_kind = AstOperatorKind::NOP;
                switch(peek())
                {
                    case Token::PLUS:
                        op_kind = AstOperatorKind::ADD;
                        break;
                    case Token::MINUS:
                        op_kind = AstOperatorKind::SUBTRACT;
                        break;

                    default:
                        return result;
                }

                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = term();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);
            }
        }

        int32_t term()
        {
            int32_t result = factor();
            while(true)
            {
                AstOperatorKind op_kind = AstOperatorKind::NOP;
                switch(peek())
                {
                    case Token::STAR:
                        op_kind = AstOperatorKind::MULTIPLY;
                        break;
                    case Token::SLASH:
                        op_kind = AstOperatorKind::DIVIDE;
                        break;
                    case Token::DOUBLESLASH:
                        op_kind = AstOperatorKind::INT_DIVIDE;
                        break;
                    case Token::PERCENT:
                        op_kind = AstOperatorKind::MODULO;
                        break;
                    case Token::AT:
                        op_kind = AstOperatorKind::MATMULT;
                        break;

                    default:
                        return result;
                }

                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = factor();
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);
            }
            return result;
        }

        int32_t factor()
        {
            AstOperatorKind op_kind = AstOperatorKind::NOP;
            switch(peek())
            {
                case Token::PLUS:
                    op_kind = AstOperatorKind::PLUS;
                    break;
                case Token::MINUS:
                    op_kind = AstOperatorKind::NEGATE;
                    break;
                case Token::TILDE:
                    op_kind = AstOperatorKind::BITWISE_NOT;
                    break;

                default:
                    return power();
            }

            uint32_t source_pos = source_pos_and_advance();
            int32_t inner = factor();
            return ast.emplace_back(
                AstKind(AstNodeKind::EXPRESSION_UNARY, op_kind), source_pos,
                inner);
        }

        int32_t power()
        {
            int32_t lhs = await_primary();
            if(peek() == Token::DOUBLESTAR)
            {
                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = power();
                return ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY,
                                                AstOperatorKind::POWER),
                                        source_pos, lhs, rhs);
            }
            else
            {
                return lhs;
            }
        }

        int32_t await_primary()
        {
            int32_t result = primary();
            while(true)
            {
                switch(peek())
                {
                    case Token::DOT:
                        {
                            uint32_t source_pos = source_pos_and_advance();
                            consume(Token::NAME);
                            std::wstring name =
                                std::wstring(string_for_name_token(
                                    *ast.compilation_unit,
                                    source_pos_for_previous_token()));
                            Value name_value =
                                vm.get_or_create_interned_string_value(name);
                            result = ast.emplace_back(
                                AstNodeKind::EXPRESSION_ATTRIBUTE, source_pos,
                                AstChildren{result}, name_value);
                            break;
                        }
                    case Token::LPAR:  // function call
                        {
                            uint32_t source_pos =
                                source_pos_and_advance();  // skip over the LPAR
                            int32_t args = arguments();
                            consume(Token::RPAR);
                            result =
                                ast.emplace_back(AstNodeKind::EXPRESSION_CALL,
                                                 source_pos, result, args);
                            break;
                        }
                    case Token::LSQB:
                        {
                            uint32_t source_pos = source_pos_and_advance();
                            int32_t index = expression();
                            consume(Token::RSQB);
                            result = ast.emplace_back(
                                AstKind(AstNodeKind::EXPRESSION_BINARY,
                                        AstOperatorKind::SUBSCRIPT),
                                source_pos, result, index);
                            break;
                        }

                    default:
                        return result;
                }
            }
        }

        int32_t arguments()
        {
            int32_t result = args();
            return result;
        }

        int32_t args()
        {
            int32_t source_pos = source_pos_for_token();
            /* TODO this is very incomplete. but just enough to get unnamed
             * arguments going */
            AstChildren ch;
            while(peek() != Token::RPAR)
            {
                ch.push_back(expression());
                if(!match(Token::COMMA))
                    break;
                if(peek() == Token::RPAR)
                    break;
            }
            return ast.emplace_back(AstNodeKind::PARAMETER_SEQUENCE, source_pos,
                                    ch);
        }

        int32_t primary() { return atom(); }

        int32_t atom()
        {
            switch(peek())
            {
                case Token::NAME:
                    {
                        std::wstring name = std::wstring(string_for_name_token(
                            *ast.compilation_unit, source_pos_for_token()));
                        Value v = vm.get_or_create_interned_string_value(name);
                        return ast.emplace_back(
                            AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                            source_pos_and_advance(), v);
                    }

                case Token::NUMBER:
                    {
                        int64_t iv = std::stoll(std::wstring(
                            string_for_number_token(*ast.compilation_unit,
                                                    source_pos_for_token())));
                        Value v = Value::from_smi(iv);
                        return ast.emplace_back(
                            AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                    AstOperatorKind::NUMBER),
                            source_pos_and_advance(), v);
                    }
                case Token::STRING:
                    {
                        std::wstring_view token = string_for_string_token(
                            *ast.compilation_unit, source_pos_for_token());
                        std::wstring value =
                            decode_python_string_literal(token);
                        Value v = vm.get_or_create_interned_string_value(value);
                        return ast.emplace_back(
                            AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                    AstOperatorKind::STRING),
                            source_pos_and_advance(), v);
                    }
                case Token::NONE:
                    return ast.emplace_back(
                        AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                AstOperatorKind::NONE),
                        source_pos_and_advance(), Value::None());
                case Token::TRUE:
                    return ast.emplace_back(
                        AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                AstOperatorKind::TRUE),
                        source_pos_and_advance(), Value::True());
                case Token::FALSE:
                    return ast.emplace_back(
                        AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                AstOperatorKind::FALSE),
                        source_pos_and_advance(), Value::False());
                case Token::LPAR:
                    {
                        uint32_t tuple_start_pos = source_pos_and_advance();
                        if(match(Token::RPAR))
                        {
                            return ast.emplace_back(
                                AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos,
                                AstChildren{});
                        }

                        int32_t result = genexp();
                        if(peek() == Token::COMMA)
                        {
                            AstChildren children = sequence_until_stop_token(
                                result, &Parser::expression, Token::COMMA,
                                Token::RPAR);
                            consume(Token::RPAR);
                            return ast.emplace_back(
                                AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos,
                                children);
                        }

                        consume(Token::RPAR);
                        return result;
                    }
                case Token::LSQB:
                    {
                        uint32_t list_start_pos = source_pos_and_advance();
                        AstChildren children;
                        if(peek() != Token::RSQB)
                        {
                            children.push_back(expression());
                            while(match(Token::COMMA))
                            {
                                if(peek() == Token::RSQB)
                                {
                                    break;
                                }
                                children.push_back(expression());
                            }
                        }
                        consume(Token::RSQB);
                        return ast.emplace_back(AstNodeKind::EXPRESSION_LIST,
                                                list_start_pos, children);
                    }
                case Token::LBRACE:
                    {
                        uint32_t dict_start_pos = source_pos_and_advance();
                        AstChildren children;
                        if(peek() != Token::RBRACE)
                        {
                            children.push_back(expression());
                            consume(Token::COLON);
                            children.push_back(expression());
                            while(match(Token::COMMA))
                            {
                                if(peek() == Token::RBRACE)
                                {
                                    break;
                                }
                                children.push_back(expression());
                                consume(Token::COLON);
                                children.push_back(expression());
                            }
                        }
                        consume(Token::RBRACE);
                        return ast.emplace_back(AstNodeKind::EXPRESSION_DICT,
                                                dict_start_pos, children);
                    }

                default:
                    uint32_t source_pos = source_pos_for_token();
                    throw std::runtime_error(std::string("Unexpected token ") +
                                             to_string(peek()) +
                                             format_error_context(source_pos));

                    // TODO NAME, STRING, parenthesis, tuples, lists, dicts, ...
                    // (ELLIPSIS)
            }
        }

        uint32_t assignment_target_source_pos(int32_t node_idx)
        {
            AstKind kind = ast.kinds[node_idx];
            switch(kind.node_kind)
            {
                case AstNodeKind::EXPRESSION_BINARY:
                case AstNodeKind::EXPRESSION_UNARY:
                case AstNodeKind::EXPRESSION_COMPARISON:
                case AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY:
                case AstNodeKind::EXPRESSION_CALL:
                case AstNodeKind::EXPRESSION_ASSIGN:
                    return assignment_target_source_pos(
                        ast.children[node_idx][0]);

                default:
                    return ast.source_offsets[node_idx];
            }
        }

        void validate_assignment_target(int32_t lhs)
        {
            if(ast.kinds[lhs].node_kind ==
                   AstNodeKind::EXPRESSION_VARIABLE_REFERENCE ||
               ast.kinds[lhs].node_kind == AstNodeKind::EXPRESSION_ATTRIBUTE ||
               (ast.kinds[lhs].node_kind == AstNodeKind::EXPRESSION_BINARY &&
                ast.kinds[lhs].operator_kind == AstOperatorKind::SUBSCRIPT))
            {
                return;
            }

            throw std::runtime_error(
                std::string("SyntaxError: assignment target must be a simple "
                            "variable, attribute, or subscript") +
                format_error_context(assignment_target_source_pos(lhs)));
        }

        uint32_t del_target_source_pos(int32_t node_idx)
        {
            return assignment_target_source_pos(node_idx);
        }

        void validate_del_target(int32_t target)
        {
            if(ast.kinds[target].node_kind ==
                   AstNodeKind::EXPRESSION_VARIABLE_REFERENCE ||
               ast.kinds[target].node_kind ==
                   AstNodeKind::EXPRESSION_ATTRIBUTE ||
               (ast.kinds[target].node_kind == AstNodeKind::EXPRESSION_BINARY &&
                ast.kinds[target].operator_kind == AstOperatorKind::SUBSCRIPT))
            {
                return;
            }

            throw std::runtime_error(
                std::string(
                    "SyntaxError: del target must be a variable, attribute, or "
                    "subscript") +
                format_error_context(del_target_source_pos(target)));
        }

        int32_t assignment()
        {
            int32_t source_pos = source_pos_for_token();
            bool lhs_parenthesized = peek() == Token::LPAR;
            int32_t lhs = star_expressions();

            if(match(Token::COLON))
            {
                validate_assignment_target(lhs);
                int32_t annotation = expression();
                AstChildren annotation_children{lhs, annotation};
                if(match(Token::EQUAL))
                {
                    annotation_children.push_back(annotated_rhs());
                }
                Value simple =
                    !lhs_parenthesized &&
                            ast.kinds[lhs].node_kind ==
                                AstNodeKind::EXPRESSION_VARIABLE_REFERENCE
                        ? Value::True()
                        : Value::False();
                return ast.emplace_back(AstNodeKind::STATEMENT_ANN_ASSIGN,
                                        source_pos, annotation_children,
                                        simple);
            }

            AstOperatorKind op_kind = AstOperatorKind::FALSE;
            switch(peek())
            {
                case Token::EQUAL:
                    op_kind = AstOperatorKind::NOP;
                    break;
                case Token::PLUSEQUAL:
                    op_kind = AstOperatorKind::ADD;
                    break;
                case Token::MINEQUAL:
                    op_kind = AstOperatorKind::SUBTRACT;
                    break;
                case Token::STAREQUAL:
                    op_kind = AstOperatorKind::MULTIPLY;
                    break;
                case Token::ATEQUAL:
                    op_kind = AstOperatorKind::MATMULT;
                    break;
                case Token::SLASHEQUAL:
                    op_kind = AstOperatorKind::DIVIDE;
                    break;
                case Token::PERCENTEQUAL:
                    op_kind = AstOperatorKind::MODULO;
                    break;
                case Token::AMPEREQUAL:
                    op_kind = AstOperatorKind::BITWISE_AND;
                    break;
                case Token::VBAREQUAL:
                    op_kind = AstOperatorKind::BITWISE_OR;
                    break;
                case Token::CIRCUMFLEXEQUAL:
                    op_kind = AstOperatorKind::BITWISE_XOR;
                    break;
                case Token::LEFTSHIFTEQUAL:
                    op_kind = AstOperatorKind::LEFTSHIFT;
                    break;
                case Token::RIGHTSHIFTEQUAL:
                    op_kind = AstOperatorKind::RIGHTSHIFT;
                    break;
                case Token::DOUBLESTAREQUAL:
                    op_kind = AstOperatorKind::POWER;
                    break;
                case Token::DOUBLESLASHEQUAL:
                    op_kind = AstOperatorKind::INT_DIVIDE;
                    break;
                default:
                    return ast.emplace_back(AstNodeKind::STATEMENT_EXPRESSION,
                                            source_pos, lhs);
            }

            validate_assignment_target(lhs);
            source_pos = source_pos_and_advance();
            int32_t rhs = annotated_rhs();
            return ast.emplace_back(
                AstKind(AstNodeKind::STATEMENT_ASSIGN, op_kind), source_pos,
                lhs, rhs);
        }

        int32_t yield_expr() { return not_implemented("yield expression"); }

        int32_t annotated_rhs()
        {
            switch(peek())
            {
                case Token::YIELD:
                    return yield_expr();
                default:
                    return star_expressions();
            }
        }

        int32_t return_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::RETURN);
            AstChildren ch;
            if(peek() != Token::NEWLINE && peek() != Token::SEMI)
            {
                ch.push_back(star_expressions());
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_RETURN, source_pos,
                                    ch);
        }

        int32_t raise_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::RAISE);
            if(peek() == Token::NEWLINE || peek() == Token::SEMI ||
               peek() == Token::DEDENT || peek() == Token::ENDMARKER)
            {
                AstChildren ch;
                return ast.emplace_back(AstNodeKind::STATEMENT_RAISE,
                                        source_pos, ch);
            }

            AstChildren ch;
            ch.push_back(expression());
            if(peek() == Token::FROM)
            {
                return not_implemented("raise from statement");
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_RAISE, source_pos,
                                    ch);
        }

        int32_t import_stmt() { return not_implemented("import statement"); }

        int32_t del_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::DEL);
            AstChildren ch;
            ch.push_back(star_expressions());
            validate_del_target(ch.back());
            while(match(Token::COMMA))
            {
                if(peek() == Token::NEWLINE || peek() == Token::SEMI)
                {
                    break;
                }
                ch.push_back(star_expressions());
                validate_del_target(ch.back());
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_DEL, source_pos, ch);
        }

        int32_t yield_stmt() { return not_implemented("yield statement"); }

        int32_t assert_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::ASSERT);
            AstChildren ch;
            ch.push_back(expression());
            if(match(Token::COMMA))
            {
                ch.push_back(expression());
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_ASSERT, source_pos,
                                    ch);
        }

        int32_t break_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::BREAK);
            return ast.emplace_back(AstNodeKind::STATEMENT_BREAK, source_pos,
                                    {});
        }

        int32_t continue_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::CONTINUE);
            return ast.emplace_back(AstNodeKind::STATEMENT_CONTINUE, source_pos,
                                    {});
        }

        int32_t pass_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::PASS);
            return ast.emplace_back(AstNodeKind::STATEMENT_PASS, source_pos,
                                    AstChildren{});
        }

        int32_t global_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::GLOBAL);

            AstChildren ch;
            do
            {
                consume(Token::NAME);
                uint32_t name_source_pos = source_pos_for_previous_token();
                std::wstring name = std::wstring(string_for_name_token(
                    *ast.compilation_unit, name_source_pos));
                Value v = vm.get_or_create_interned_string_value(name);
                ch.push_back(
                    ast.emplace_back(AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                                     name_source_pos, v));
            }
            while(match(Token::COMMA));

            return ast.emplace_back(AstNodeKind::STATEMENT_GLOBAL, source_pos,
                                    ch);
        }

        int32_t nonlocal_stmt()
        {
            return not_implemented("nonlocal statement");
        }

        int32_t simple_stmt()
        {
            switch(peek())
            {
                case Token::RETURN:
                    return return_stmt();
                case Token::RAISE:
                    return raise_stmt();
                case Token::IMPORT:
                case Token::FROM:
                    return import_stmt();

                case Token::DEL:
                    return del_stmt();
                case Token::YIELD:
                    return yield_stmt();
                case Token::ASSERT:
                    return assert_stmt();
                case Token::BREAK:
                    return break_stmt();
                case Token::CONTINUE:
                    return continue_stmt();
                case Token::PASS:
                    return pass_stmt();
                case Token::GLOBAL:
                    return global_stmt();
                case Token::NONLOCAL:
                    return nonlocal_stmt();

                default:
                    return assignment();
            }
        }

        int32_t simple_stmts()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            do
            {

                children.emplace_back(simple_stmt());
            }
            while(match(Token::SEMI));

            consume(Token::NEWLINE);

            if(children.size() == 1)
            {
                return children[0];
            }
            else
            {
                return ast.emplace_back(AstNodeKind::STATEMENT_SEQUENCE,
                                        source_pos, children);
            }
        }

        int32_t function_def()
        {
            if(peek() == Token::AT)
            {
                // todo worry about decorators later
            }
            return function_def_raw();
        }

        int32_t function_def_raw()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::DEF);  // todo worry about async later
            consume(Token::NAME);
            std::wstring name = std::wstring(string_for_name_token(
                *ast.compilation_unit, source_pos_for_previous_token()));
            Value name_str = vm.get_or_create_interned_string_value(name);
            consume(Token::LPAR);
            int32_t param_seq = params();
            consume(Token::RPAR);
            if(peek() == Token::RARROW)
            {
                consume(Token::RARROW);
                expression();  // just swallow that return type definition, we
                               // ignore it
            }

            consume(Token::COLON);
            // todo worry about func_type_comment later
            int32_t body = block();
            return ast.emplace_back(AstNodeKind::STATEMENT_FUNCTION_DEF,
                                    source_pos, {param_seq, body}, name_str);
        }

        int32_t params() { return parameters(); }

        int32_t parameters()
        {
            int32_t source_pos = source_pos_for_token();
            AstChildren ch;
            bool seen_default = false;
            while(peek() != Token::RPAR)
            {
                bool is_varargs = match(Token::STAR);
                consume(Token::NAME);
                uint32_t name_source_pos = source_pos_for_previous_token();
                std::wstring name = std::wstring(string_for_name_token(
                    *ast.compilation_unit, name_source_pos));
                Value v = vm.get_or_create_interned_string_value(name);
                AstChildren parameter_children;
                if(match(Token::COLON))
                {
                    expression();
                }
                if(is_varargs)
                {
                    if(match(Token::EQUAL))
                    {
                        throw std::runtime_error(
                            "SyntaxError: varargs parameter cannot have a "
                            "default");
                    }
                    ch.push_back(ast.emplace_back(
                        AstNodeKind::PARAMETER_VARARGS, name_source_pos,
                        parameter_children, v));
                    if(match(Token::COMMA) && peek() != Token::RPAR)
                    {
                        if(peek() == Token::STAR)
                        {
                            throw std::runtime_error(
                                "SyntaxError: * argument may appear only once");
                        }
                        throw std::runtime_error(
                            "SyntaxError: keyword-only parameters are not "
                            "implemented yet");
                    }
                    break;
                }
                if(match(Token::EQUAL))
                {
                    seen_default = true;
                    parameter_children.push_back(expression());
                }
                else if(seen_default)
                {
                    throw std::runtime_error(
                        "SyntaxError: non-default argument follows default "
                        "argument");
                }
                ch.push_back(ast.emplace_back(AstNodeKind::PARAMETER,
                                              name_source_pos,
                                              parameter_children, v));
                if(!match(Token::COMMA))
                    break;
                if(peek() == Token::RPAR)
                    break;
            }
            return ast.emplace_back(AstNodeKind::PARAMETER_SEQUENCE, source_pos,
                                    ch);
        }

        int32_t if_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            consume(Token::IF);
            children.push_back(named_expression());
            consume(Token::COLON);
            children.push_back(block());
            while(peek() == Token::ELIF)
            {
                consume(Token::ELIF);
                children.push_back(named_expression());
                consume(Token::COLON);
                children.push_back(block());
            }
            if(peek() == Token::ELSE)
            {
                consume(Token::ELSE);
                consume(Token::COLON);
                children.push_back(block());
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_IF, source_pos,
                                    children);
        }

        int32_t class_def()
        {
            int32_t source_pos = source_pos_for_token();
            consume(Token::CLASS);
            consume(Token::NAME);
            std::wstring name = std::wstring(string_for_name_token(
                *ast.compilation_unit, source_pos_for_previous_token()));
            Value name_str = vm.get_or_create_interned_string_value(name);

            AstChildren children;
            int32_t bases = ast.emplace_back(AstNodeKind::PARAMETER_SEQUENCE,
                                             source_pos, AstChildren{});
            if(match(Token::LPAR))
            {
                bases = args();
                consume(Token::RPAR);
            }
            children.push_back(bases);

            consume(Token::COLON);
            children.push_back(block());
            return ast.emplace_back(AstNodeKind::STATEMENT_CLASS_DEF,
                                    source_pos, children, name_str);
        }

        int32_t with_stmt() { return not_implemented("with statement"); }

        int32_t for_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            consume(Token::FOR);
            uint32_t target_start_pos = source_pos_for_token();
            int32_t target = -1;
            if(peek() == Token::NAME)
            {
                std::wstring name = std::wstring(string_for_name_token(
                    *ast.compilation_unit, source_pos_for_token()));
                Value v = vm.get_or_create_interned_string_value(name);
                target =
                    ast.emplace_back(AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                                     source_pos_and_advance(), v);
                if(peek() == Token::COMMA)
                {
                    AstChildren target_children = sequence_until_stop_token(
                        target, &Parser::star_expression, Token::COMMA,
                        Token::IN);
                    target =
                        ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE,
                                         target_start_pos, target_children);
                }
            }
            else
            {
                target = star_expressions();
            }
            validate_assignment_target(target);
            children.push_back(target);
            consume(Token::IN);
            children.push_back(named_expression());
            consume(Token::COLON);
            children.push_back(block());

            if(peek() == Token::ELSE)
            {
                consume(Token::ELSE);
                consume(Token::COLON);
                children.push_back(block());
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_FOR, source_pos,
                                    children);
        }

        int32_t try_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            consume(Token::TRY);
            consume(Token::COLON);
            children.push_back(block());

            if(peek() != Token::EXCEPT)
            {
                return not_implemented("try statement without except");
            }

            bool saw_bare_except = false;
            while(peek() == Token::EXCEPT)
            {
                if(saw_bare_except)
                {
                    return not_implemented("except after bare except");
                }
                int32_t handler_source_pos = source_pos_for_token();
                consume(Token::EXCEPT);
                AstChildren handler_children;
                if(peek() != Token::COLON)
                {
                    handler_children.push_back(expression());
                    if(match(Token::AS))
                    {
                        consume(Token::NAME);
                        uint32_t name_source_pos =
                            source_pos_for_previous_token();
                        std::wstring name = std::wstring(string_for_name_token(
                            *ast.compilation_unit, name_source_pos));
                        Value v = vm.get_or_create_interned_string_value(name);
                        handler_children.push_back(ast.emplace_back(
                            AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                            name_source_pos, v));
                    }
                }
                else
                {
                    saw_bare_except = true;
                }
                consume(Token::COLON);
                handler_children.push_back(block());
                children.push_back(
                    ast.emplace_back(AstNodeKind::STATEMENT_EXCEPT_HANDLER,
                                     handler_source_pos, handler_children));
            }
            if(peek() == Token::ELSE || peek() == Token::FINALLY)
            {
                return not_implemented("try else/finally");
            }

            return ast.emplace_back(AstNodeKind::STATEMENT_TRY, source_pos,
                                    children);
        }

        int32_t while_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            consume(Token::WHILE);
            children.push_back(named_expression());
            consume(Token::COLON);
            children.push_back(block());

            if(peek() == Token::ELSE)
            {
                consume(Token::ELSE);
                consume(Token::COLON);
                children.push_back(block());
            }
            return ast.emplace_back(AstNodeKind::STATEMENT_WHILE, source_pos,
                                    children);
        }

        int32_t match_stmt() { return not_implemented("match statement"); }

        int32_t compound_statement()
        {
            switch(peek())
            {
                case Token::DEF:
                case Token::AT:
                case Token::ASYNC:
                    return function_def();
                case Token::IF:
                    return if_stmt();
                case Token::CLASS:
                    return class_def();
                case Token::WITH:
                    return with_stmt();
                case Token::FOR:
                    return for_stmt();
                case Token::TRY:
                    return try_stmt();
                case Token::WHILE:
                    return while_stmt();

                default:
                    return match_stmt();
            }
        }

        int32_t statement()
        {
            switch(peek())
            {
                case Token::DEF:
                case Token::AT:
                case Token::ASYNC:
                case Token::IF:
                case Token::CLASS:
                case Token::WITH:
                case Token::FOR:
                case Token::TRY:
                case Token::WHILE:
                    return compound_statement();

                default:
                    return simple_stmts();
            }
        }

        int32_t statements()
        {
            int32_t source_pos = source_pos_for_token();
            AstChildren children;
            do
            {
                children.push_back(statement());
            }
            while(peek() != Token::DEDENT && peek() != Token::ENDMARKER);
            return ast.emplace_back(AstNodeKind::STATEMENT_SEQUENCE, source_pos,
                                    children);
        }

        int32_t file()
        {
            while(match(Token::NEWLINE))
            {
            }
            int32_t idx = -1;
            if(!is_at_end())
            {
                idx = statements();
            }
            consume(Token::ENDMARKER);
            return idx;
        }

        int32_t eval()
        {
            int32_t result = expressions();
            while(match(Token::NEWLINE))
            {
            }

            consume(Token::ENDMARKER);
            return result;
        }
    };

    AstVector parse(VirtualMachine &vm, const TokenVector &tv,
                    StartRule start_rule)
    {
        Parser parser(vm, tv);

        return parser.parse(start_rule);
    }

}  // namespace cl
