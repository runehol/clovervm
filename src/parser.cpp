#include "parser.h"

#include "ast.h"
#include "compilation_unit.h"
#include "fatal.h"
#include "float.h"
#include "thread_state.h"
#include "token.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>

namespace cl
{
    template <typename T>
    static Expected<T> raise_parse_exception(const wchar_t *type_name,
                                             std::wstring message)
    {
        return Expected<T>::raise_exception(type_name, message.c_str());
    }

    static std::string remove_number_separators(std::wstring_view token)
    {
        std::string result;
        result.reserve(token.size());
        for(wchar_t c: token)
        {
            if(c != L'_')
            {
                result.push_back(static_cast<char>(c));
            }
        }
        return result;
    }

    static int64_t parse_saturated_decimal_exponent(std::string_view text)
    {
        bool negative = false;
        if(!text.empty() && (text.front() == '+' || text.front() == '-'))
        {
            negative = text.front() == '-';
            text.remove_prefix(1);
        }

        int64_t exponent = 0;
        constexpr int64_t max_relevant_decimal_exponent = 1000000;
        for(char c: text)
        {
            if(exponent < max_relevant_decimal_exponent)
            {
                exponent = exponent * 10 + (c - '0');
            }
        }
        if(exponent > max_relevant_decimal_exponent)
        {
            exponent = max_relevant_decimal_exponent;
        }
        return negative ? -exponent : exponent;
    }

    static bool float_literal_range_error_is_underflow(std::string_view literal)
    {
        size_t exponent_idx = literal.find_first_of("eE");
        std::string_view significand = exponent_idx == std::string_view::npos
                                           ? literal
                                           : literal.substr(0, exponent_idx);
        int64_t exponent = exponent_idx == std::string_view::npos
                               ? 0
                               : parse_saturated_decimal_exponent(
                                     literal.substr(exponent_idx + 1));

        int64_t digits_before_point = 0;
        int64_t digit_idx = 0;
        int64_t first_nonzero_digit_idx = -1;
        bool before_point = true;
        for(char c: significand)
        {
            if(c == '.')
            {
                before_point = false;
                continue;
            }
            if(before_point)
            {
                digits_before_point++;
            }
            if(c != '0' && first_nonzero_digit_idx < 0)
            {
                first_nonzero_digit_idx = digit_idx;
            }
            digit_idx++;
        }

        if(first_nonzero_digit_idx < 0)
        {
            return true;
        }

        int64_t adjusted_exponent =
            digits_before_point - first_nonzero_digit_idx - 1 + exponent;
        return adjusted_exponent < 0;
    }

    static double parse_float_literal(std::wstring_view token)
    {
        std::string literal = remove_number_separators(token);
        double value = 0.0;
        const char *begin = literal.data();
        const char *end = begin + literal.size();
        std::from_chars_result result = std::from_chars(begin, end, value);
        if(result.ec == std::errc() && result.ptr == end)
        {
            return value;
        }
        if(result.ec == std::errc::result_out_of_range && result.ptr == end)
        {
            if(float_literal_range_error_is_underflow(literal))
            {
                return 0.0;
            }
            return std::numeric_limits<double>::infinity();
        }
        fatal("tokenizer accepted invalid float literal");
    }

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

    static Expected<std::wstring>
    decode_python_string_literal(std::wstring_view token)
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
            return Expected<std::wstring>::raise_exception(
                L"SyntaxError", L"Invalid string literal");
        }

        wchar_t quote = token[prefix_len];
        if(quote != L'\'' && quote != L'"')
        {
            return Expected<std::wstring>::raise_exception(
                L"SyntaxError", L"Invalid string literal");
        }
        size_t quote_len = 1;
        if(prefix_len + 2 < token.size() && token[prefix_len + 1] == quote &&
           token[prefix_len + 2] == quote)
        {
            quote_len = 3;
        }
        if(token.size() < prefix_len + quote_len * 2)
        {
            return Expected<std::wstring>::raise_exception(
                L"SyntaxError", L"Invalid string literal");
        }
        for(size_t i = 0; i < quote_len; ++i)
        {
            if(token[token.size() - quote_len + i] != quote)
            {
                return Expected<std::wstring>::raise_exception(
                    L"SyntaxError", L"Invalid string literal");
            }
        }

        std::wstring body = std::wstring(token.substr(
            prefix_len + quote_len, token.size() - prefix_len - quote_len * 2));
        if(is_raw)
        {
            return Expected<std::wstring>::ok(std::move(body));
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
                return Expected<std::wstring>::raise_exception(
                    L"SyntaxError", L"Invalid escape in string literal");
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
                            return Expected<std::wstring>::raise_exception(
                                L"SyntaxError",
                                L"Invalid \\x escape in string literal");
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
                            return Expected<std::wstring>::raise_exception(
                                L"SyntaxError",
                                L"Invalid unicode escape in string literal");
                        }
                        uint32_t value = 0;
                        for(size_t j = 1; j <= digits; ++j)
                        {
                            wchar_t h = body[i + j];
                            if(!is_hex_digit(h))
                            {
                                return Expected<std::wstring>::raise_exception(
                                    L"SyntaxError", L"Invalid unicode escape "
                                                    L"in string literal");
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
        return Expected<std::wstring>::ok(std::move(out));
    }

    class Parser
    {
    public:
        Parser(VirtualMachine &_vm, const TokenVector &_token_vec,
               CompileContinuationInfo *_compile_continuation_info)
            : vm(_vm), token_vec(_token_vec), ast(token_vec.compilation_unit),
              compile_continuation_info(_compile_continuation_info)
        {
            if(compile_continuation_info != nullptr)
            {
                *compile_continuation_info = CompileContinuationInfo{};
            }
        }

        Expected<AstVector> parse(StartRule start_rule)
        {
            switch(start_rule)
            {
                case StartRule::File:
                    ast.root_node = CL_TRY(file());
                    break;
                case StartRule::Interactive:
                    ast.root_node = CL_TRY(interactive());
                    break;
                case StartRule::Eval:
                    ast.root_node = CL_TRY(eval());
                    break;
                case StartRule::FuncType:
                    break;
                case StartRule::FString:
                    break;
            }

            return Expected<AstVector>::ok(std::move(ast));
        }

    private:
        VirtualMachine &vm;
        const TokenVector &token_vec;
        AstVector ast;
        CompileContinuationInfo *compile_continuation_info;
        size_t token_pos = 0;
        uint32_t indentation_level = 0;

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
            Token token = token_vec.tokens[token_pos++];
            update_indentation_level(token);
            return token;
        };

        uint32_t source_pos_and_advance()
        {
            assert(token_pos < token_vec.size());
            uint32_t source_pos = token_vec.source_offsets[token_pos];
            update_indentation_level(token_vec.tokens[token_pos]);
            ++token_pos;
            return source_pos;
        }

        Expected<void> consume(Token expected)
        {
            uint32_t previous_indentation_level = indentation_level;
            uint32_t source_pos = source_pos_for_token();
            Token actual = advance();
            if(expected != actual)
            {
                if(is_tokenizer_error(actual))
                {
                    return raise_parse_error_for_tokenizer_error<void>(
                        actual, source_pos, previous_indentation_level);
                }
                std::wstring message =
                    expected_token_message(expected, actual, source_pos);
                if(expected == Token::INDENT &&
                   (actual == Token::ENDMARKER || actual == Token::DEDENT))
                {
                    return raise_parse_error<void>(
                        {std::move(message), true,
                         previous_indentation_level + 1});
                }
                return raise_parse_error<void>({std::move(message)});
            }
            return Expected<void>::ok();
        }

        bool match(Token expected)
        {
            Token actual = peek();
            if(actual == expected)
            {
                (void)advance();
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

        static bool is_tokenizer_error(Token token)
        {
            return token == Token::ERRORTOKEN_INVALID_CHARACTER ||
                   token == Token::ERRORTOKEN_UNTERMINATED_STRING ||
                   token == Token::ERRORTOKEN_UNTERMINATED_TRIPLE_STRING ||
                   token == Token::ERRORTOKEN_OPEN_BRACKET_EOF;
        }

        std::wstring expected_token_message(Token expected, Token actual,
                                            uint32_t source_pos)
        {
            std::wstring message = L"Expected token ";
            message += to_wstring(expected);
            message += L", got ";
            message += to_wstring(actual);
            message += format_error_context(source_pos);
            return message;
        }

        std::wstring unexpected_token_message(Token token, uint32_t source_pos)
        {
            std::wstring message = L"Unexpected token ";
            message += to_wstring(token);
            message += format_error_context(source_pos);
            return message;
        }

        struct ParseFailureDetails
        {
            std::wstring message;
            bool incomplete_input = false;
            uint32_t next_indentation_level = 0;
        };

        template <typename T>
        Expected<T> raise_parse_error(ParseFailureDetails details)
        {
            if(compile_continuation_info != nullptr)
            {
                compile_continuation_info->incomplete_input =
                    details.incomplete_input;
                compile_continuation_info->next_indentation_level =
                    details.next_indentation_level;
            }
            return raise_parse_exception<T>(L"SyntaxError",
                                            std::move(details.message));
        }

        ParseFailureDetails parse_error_details_for_tokenizer_error(
            Token token, uint32_t source_pos, uint32_t error_indentation_level)
        {
            switch(token)
            {
                case Token::ERRORTOKEN_INVALID_CHARACTER:
                    return {invalid_character_message(source_pos) +
                            format_error_context(source_pos)};
                case Token::ERRORTOKEN_UNTERMINATED_STRING:
                    return {L"unterminated string literal" +
                            format_error_context(source_pos)};
                case Token::ERRORTOKEN_UNTERMINATED_TRIPLE_STRING:
                    return {L"unterminated triple-quoted string "
                            L"literal" +
                                format_error_context(source_pos),
                            true, error_indentation_level};
                case Token::ERRORTOKEN_OPEN_BRACKET_EOF:
                    return {L"incomplete input" +
                                format_error_context(source_pos),
                            true, error_indentation_level};
                default:
                    return {unexpected_token_message(token, source_pos)};
            }
        }

        template <typename T>
        Expected<T>
        raise_parse_error_for_tokenizer_error(Token token, uint32_t source_pos,
                                              uint32_t error_indentation_level)
        {
            return raise_parse_error<T>(parse_error_details_for_tokenizer_error(
                token, source_pos, error_indentation_level));
        }

        std::wstring invalid_character_message(uint32_t source_pos)
        {
            const std::wstring &source = ast.compilation_unit->source_code;
            uint32_t code_point =
                source_pos < source.size() ? source[source_pos] : 0;
            cl_wchar invalid_char = static_cast<cl_wchar>(code_point);

            std::wostringstream out;
            out.imbue(std::locale::classic());
            out << L"invalid character '" << invalid_char << L"' (U+"
                << std::uppercase << std::hex << std::setw(4)
                << std::setfill(L'0') << code_point << L")";
            return out.str();
        }

        void update_indentation_level(Token token)
        {
            if(token == Token::INDENT)
            {
                ++indentation_level;
            }
            else if(token == Token::DEDENT)
            {
                assert(indentation_level > 0);
                --indentation_level;
            }
        }

        std::wstring format_error_context(uint32_t source_pos)
        {
            auto [line, column] =
                ast.compilation_unit->get_line_column(source_pos);
            std::wstring_view line_view =
                ast.compilation_unit->get_line_view(source_pos);
            std::wstring snippet(line_view);
            static constexpr size_t max_snippet_len = 40;
            if(snippet.size() > max_snippet_len)
            {
                snippet.resize(max_snippet_len);
                snippet += L"...";
            }

            return L" at offset " + std::to_wstring(source_pos) + L" (line " +
                   std::to_wstring(line) + L", column " +
                   std::to_wstring(column) + L"), near \"" + snippet + L"\"";
        }

        Expected<int32_t> not_implemented(const wchar_t *construct_name)
        {
            std::wstring message = L"Not implemented: ";
            message += construct_name;
            message += L" (token ";
            message += to_wstring(peek());
            message += L")";
            message += format_error_context(source_pos_for_token());
            return Expected<int32_t>::raise_exception(L"SyntaxError",
                                                      message.c_str());
        }

        // now the parser itself

        // helper to build sequences
        Expected<AstChildren>
        sequence_until_stop_token(int32_t initial,
                                  Expected<int32_t> (Parser::*parse_member)(),
                                  Token separator, Token stop)
        {
            AstChildren children;
            children.push_back(initial);
            while(match(separator))
            {
                if(peek() == stop)
                    break;

                int32_t member = CL_TRY((this->*parse_member)());
                children.push_back(member);
            }
            return Expected<AstChildren>::ok(std::move(children));
        }

        Expected<int32_t> block()
        {
            int32_t stmts = -1;
            if(match(Token::NEWLINE))
            {
                CL_TRY(consume(Token::INDENT));
                stmts = CL_TRY(statements());
                CL_TRY(consume(Token::DEDENT));
            }
            else
            {
                stmts = CL_TRY(simple_stmts());
            }
            return Expected<int32_t>::ok(stmts);
        }
        // expressions

        Expected<int32_t> genexp() { return expression(); }

        Expected<int32_t> star_expressions()
        {
            uint32_t tuple_start_pos = source_pos_for_token();
            int32_t result = CL_TRY(star_expression());
            if(peek() == Token::COMMA)
            {
                AstChildren children = CL_TRY(
                    sequence_until_stop_token(result, &Parser::star_expression,
                                              Token::COMMA, Token::NEWLINE));
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos, children));
            }
            else
            {
                return Expected<int32_t>::ok(result);
            }
        }

        Expected<int32_t> star_expression() { return expression(); }

        Expected<int32_t> expressions()
        {
            uint32_t tuple_start_pos = source_pos_for_token();
            int32_t result = CL_TRY(expression());
            if(peek() == Token::COMMA)
            {
                AstChildren children = CL_TRY(sequence_until_stop_token(
                    result, &Parser::expression, Token::COMMA, Token::NEWLINE));
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos, children));
            }
            else
            {
                return Expected<int32_t>::ok(result);
            }
        }

        Expected<int32_t> expression() { return disjunction(); }

        Expected<int32_t> assignment_expression()
        {
            if(peek() != Token::NAME)
            {
                uint32_t source_pos = source_pos_for_token();
                std::wstring message =
                    expected_token_message(Token::NAME, peek(), source_pos);
                return Expected<int32_t>::raise_exception(L"SyntaxError",
                                                          message.c_str());
            }
            int32_t lhs = CL_TRY(atom());  // smallest rule that just consumes a
                                           // name and makes a nice node for us
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::COLONEQUAL));
            int32_t rhs = CL_TRY(expression());
            return Expected<int32_t>::ok(ast.emplace_back(
                AstKind(AstNodeKind::EXPRESSION_ASSIGN, AstOperatorKind::NOP),
                source_pos, lhs, rhs));
        }

        Expected<int32_t> named_expression()
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

        Expected<int32_t> disjunction()
        {
            int32_t lhs = CL_TRY(conjunction());
            if(peek() == Token::OR)
            {
                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = CL_TRY(disjunction());
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY,
                            AstOperatorKind::SHORTCUTTING_OR),
                    source_pos, lhs, rhs));
            }
            else
            {
                return Expected<int32_t>::ok(lhs);
            }
        }

        Expected<int32_t> conjunction()
        {
            int32_t lhs = CL_TRY(inversion());
            if(peek() == Token::AND)
            {
                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = CL_TRY(conjunction());
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_SHORTCUTTING_BINARY,
                            AstOperatorKind::SHORTCUTTING_AND),
                    source_pos, lhs, rhs));
            }
            else
            {
                return Expected<int32_t>::ok(lhs);
            }
        }

        Expected<int32_t> inversion()
        {
            if(match(Token::NOT))
            {
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_UNARY,
                            AstOperatorKind::NOT),
                    source_pos_for_previous_token(), CL_TRY(inversion())));
            }
            return comparison();
        }

        Expected<int32_t> comparison()
        {
            AstChildren ch;
            ch.push_back(CL_TRY(bitwise_or()));
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
                ch.push_back(CL_TRY(comparison_fragment()));
            }

            if(ch.size() == 1)
            {
                return Expected<int32_t>::ok(ch[0]);
            }
            else
            {
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstNodeKind::EXPRESSION_COMPARISON, source_pos, ch));
            }
        }

        Expected<int32_t> comparison_fragment()
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
                CL_TRY(consume(Token::NOT));
                CL_TRY(consume(Token::IN));
                ok = AstOperatorKind::NOT_IN;
            }
            assert(ok != AstOperatorKind::NOP);
            int32_t ch = CL_TRY(bitwise_or());
            return Expected<int32_t>::ok(ast.emplace_back(
                AstKind(AstNodeKind::EXPRESSION_COMPARISON_FRAGMENT, ok),
                source_pos, ch));
        }

        Expected<int32_t> bitwise_or()
        {
            int32_t result = CL_TRY(bitwise_xor());
            while(match(Token::VBAR))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = CL_TRY(bitwise_xor());
                result =
                    ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY,
                                             AstOperatorKind::BITWISE_OR),
                                     source_pos, result, rhs);
            }
            return Expected<int32_t>::ok(result);
        }

        Expected<int32_t> bitwise_xor()
        {
            int32_t result = CL_TRY(bitwise_and());
            while(match(Token::CIRCUMFLEX))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = CL_TRY(bitwise_and());
                result =
                    ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY,
                                             AstOperatorKind::BITWISE_XOR),
                                     source_pos, result, rhs);
            }
            return Expected<int32_t>::ok(result);
        }

        Expected<int32_t> bitwise_and()
        {
            int32_t result = CL_TRY(shift_expr());
            while(match(Token::AMPER))
            {
                uint32_t source_pos = source_pos_for_previous_token();
                int32_t rhs = CL_TRY(shift_expr());
                result =
                    ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY,
                                             AstOperatorKind::BITWISE_AND),
                                     source_pos, result, rhs);
            }
            return Expected<int32_t>::ok(result);
        }

        Expected<int32_t> shift_expr()
        {
            int32_t result = CL_TRY(sum());
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
                        return Expected<int32_t>::ok(result);
                }

                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = CL_TRY(sum());
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);
            }
        }

        Expected<int32_t> sum()
        {
            int32_t result = CL_TRY(term());
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
                        return Expected<int32_t>::ok(result);
                }

                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = CL_TRY(term());
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);
            }
        }

        Expected<int32_t> term()
        {
            int32_t result = CL_TRY(factor());
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
                        return Expected<int32_t>::ok(result);
                }

                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = CL_TRY(factor());
                result = ast.emplace_back(
                    AstKind(AstNodeKind::EXPRESSION_BINARY, op_kind),
                    source_pos, result, rhs);
            }
        }

        Expected<int32_t> factor()
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
            int32_t inner = CL_TRY(factor());
            return Expected<int32_t>::ok(ast.emplace_back(
                AstKind(AstNodeKind::EXPRESSION_UNARY, op_kind), source_pos,
                inner));
        }

        Expected<int32_t> power()
        {
            int32_t lhs = CL_TRY(await_primary());
            if(peek() == Token::DOUBLESTAR)
            {
                uint32_t source_pos = source_pos_and_advance();
                int32_t rhs = CL_TRY(power());
                return Expected<int32_t>::ok(
                    ast.emplace_back(AstKind(AstNodeKind::EXPRESSION_BINARY,
                                             AstOperatorKind::POWER),
                                     source_pos, lhs, rhs));
            }
            else
            {
                return Expected<int32_t>::ok(lhs);
            }
        }

        Expected<int32_t> await_primary()
        {
            int32_t result = CL_TRY(primary());
            while(true)
            {
                switch(peek())
                {
                    case Token::DOT:
                        {
                            uint32_t source_pos = source_pos_and_advance();
                            CL_TRY(consume(Token::NAME));
                            std::wstring name =
                                std::wstring(string_for_name_token(
                                    *ast.compilation_unit,
                                    source_pos_for_previous_token()));
                            TValue<String> name_value =
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
                            int32_t args = CL_TRY(arguments());
                            CL_TRY(consume(Token::RPAR));
                            result =
                                ast.emplace_back(AstNodeKind::EXPRESSION_CALL,
                                                 source_pos, result, args);
                            break;
                        }
                    case Token::LSQB:
                        {
                            uint32_t source_pos = source_pos_and_advance();
                            int32_t index = CL_TRY(expression());
                            CL_TRY(consume(Token::RSQB));
                            result = ast.emplace_back(
                                AstKind(AstNodeKind::EXPRESSION_BINARY,
                                        AstOperatorKind::SUBSCRIPT),
                                source_pos, result, index);
                            break;
                        }

                    default:
                        return Expected<int32_t>::ok(result);
                }
            }
        }

        Expected<int32_t> arguments() { return args(); }

        Expected<int32_t> args()
        {
            int32_t source_pos = source_pos_for_token();
            AstChildren ch;
            std::unordered_set<std::wstring> keyword_names;
            bool seen_keyword = false;
            while(peek() != Token::RPAR)
            {
                if(peek() == Token::STAR)
                {
                    return Expected<int32_t>::raise_exception(
                        L"SyntaxError",
                        L"starred call arguments are not implemented yet");
                }
                if(peek() == Token::DOUBLESTAR)
                {
                    return Expected<int32_t>::raise_exception(
                        L"SyntaxError",
                        L"keyword argument unpacking is not implemented yet");
                }
                if(peek() == Token::NAME && peek2() == Token::EQUAL)
                {
                    uint32_t name_source_pos = source_pos_and_advance();
                    std::wstring name = std::wstring(string_for_name_token(
                        *ast.compilation_unit, name_source_pos));
                    if(!keyword_names.insert(name).second)
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError", L"keyword argument repeated");
                    }
                    TValue<String> name_value =
                        vm.get_or_create_interned_string_value(name);
                    CL_TRY(consume(Token::EQUAL));
                    int32_t value = CL_TRY(expression());
                    ch.push_back(ast.emplace_back(
                        AstNodeKind::CALL_ARGUMENT_KEYWORD, name_source_pos,
                        AstChildren{value}, name_value));
                    seen_keyword = true;
                }
                else
                {
                    if(seen_keyword)
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError",
                            L"positional argument follows keyword argument");
                    }
                    int32_t value = CL_TRY(expression());
                    ch.push_back(ast.emplace_back(
                        AstNodeKind::CALL_ARGUMENT_POSITIONAL,
                        ast.source_offsets[value], AstChildren{value}));
                }
                if(!match(Token::COMMA))
                    break;
                if(peek() == Token::RPAR)
                    break;
            }
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::PARAMETER_SEQUENCE, source_pos, ch));
        }

        Expected<int32_t> primary() { return atom(); }

        Expected<int32_t> atom()
        {
            switch(peek())
            {
                case Token::NAME:
                    {
                        std::wstring name = std::wstring(string_for_name_token(
                            *ast.compilation_unit, source_pos_for_token()));
                        TValue<String> v =
                            vm.get_or_create_interned_string_value(name);
                        return Expected<int32_t>::ok(ast.emplace_back(
                            AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                            source_pos_and_advance(), v));
                    }

                case Token::INT_NUMBER:
                    {
                        std::wstring_view token = string_for_int_number_token(
                            *ast.compilation_unit, source_pos_for_token());
                        int64_t iv = std::stoll(std::wstring(token));
                        Value v = Value::from_smi(iv);
                        return Expected<int32_t>::ok(ast.emplace_back(
                            AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                    AstOperatorKind::NUMBER),
                            source_pos_and_advance(), v));
                    }
                case Token::FLOAT_NUMBER:
                    {
                        std::wstring_view token = string_for_float_number_token(
                            *ast.compilation_unit, source_pos_for_token());
                        double value = parse_float_literal(token);
                        TValue<Float> v = make_object_value<Float>(value);
                        return Expected<int32_t>::ok(ast.emplace_back(
                            AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                    AstOperatorKind::NUMBER),
                            source_pos_and_advance(), v));
                    }
                case Token::STRING:
                    {
                        std::wstring_view token = string_for_string_token(
                            *ast.compilation_unit, source_pos_for_token());
                        std::wstring value =
                            CL_TRY(decode_python_string_literal(token));
                        TValue<String> v =
                            vm.get_or_create_interned_string_value(value);
                        return Expected<int32_t>::ok(ast.emplace_back(
                            AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                    AstOperatorKind::STRING),
                            source_pos_and_advance(), v));
                    }
                case Token::NONE:
                    return Expected<int32_t>::ok(ast.emplace_back(
                        AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                AstOperatorKind::NONE),
                        source_pos_and_advance(), Value::None()));
                case Token::TRUE:
                    return Expected<int32_t>::ok(ast.emplace_back(
                        AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                AstOperatorKind::TRUE),
                        source_pos_and_advance(), Value::True()));
                case Token::FALSE:
                    return Expected<int32_t>::ok(ast.emplace_back(
                        AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                AstOperatorKind::FALSE),
                        source_pos_and_advance(), Value::False()));
                case Token::ELLIPSIS:
                    return Expected<int32_t>::ok(ast.emplace_back(
                        AstKind(AstNodeKind::EXPRESSION_LITERAL,
                                AstOperatorKind::ELLIPSIS),
                        source_pos_and_advance(), Value::Ellipsis()));
                case Token::LPAR:
                    {
                        uint32_t tuple_start_pos = source_pos_and_advance();
                        if(match(Token::RPAR))
                        {
                            return Expected<int32_t>::ok(ast.emplace_back(
                                AstNodeKind::EXPRESSION_TUPLE, tuple_start_pos,
                                AstChildren{}));
                        }

                        int32_t result = CL_TRY(genexp());
                        if(peek() == Token::COMMA)
                        {
                            AstChildren children =
                                CL_TRY(sequence_until_stop_token(
                                    result, &Parser::expression, Token::COMMA,
                                    Token::RPAR));
                            CL_TRY(consume(Token::RPAR));
                            return Expected<int32_t>::ok(
                                ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE,
                                                 tuple_start_pos, children));
                        }

                        CL_TRY(consume(Token::RPAR));
                        return Expected<int32_t>::ok(result);
                    }
                case Token::LSQB:
                    {
                        uint32_t list_start_pos = source_pos_and_advance();
                        AstChildren children;
                        if(peek() != Token::RSQB)
                        {
                            children.push_back(CL_TRY(expression()));
                            while(match(Token::COMMA))
                            {
                                if(peek() == Token::RSQB)
                                {
                                    break;
                                }
                                children.push_back(CL_TRY(expression()));
                            }
                        }
                        CL_TRY(consume(Token::RSQB));
                        return Expected<int32_t>::ok(
                            ast.emplace_back(AstNodeKind::EXPRESSION_LIST,
                                             list_start_pos, children));
                    }
                case Token::LBRACE:
                    {
                        uint32_t dict_start_pos = source_pos_and_advance();
                        AstChildren children;
                        if(peek() != Token::RBRACE)
                        {
                            children.push_back(CL_TRY(expression()));
                            CL_TRY(consume(Token::COLON));
                            children.push_back(CL_TRY(expression()));
                            while(match(Token::COMMA))
                            {
                                if(peek() == Token::RBRACE)
                                {
                                    break;
                                }
                                children.push_back(CL_TRY(expression()));
                                CL_TRY(consume(Token::COLON));
                                children.push_back(CL_TRY(expression()));
                            }
                        }
                        CL_TRY(consume(Token::RBRACE));
                        return Expected<int32_t>::ok(
                            ast.emplace_back(AstNodeKind::EXPRESSION_DICT,
                                             dict_start_pos, children));
                    }

                default:
                    uint32_t source_pos = source_pos_for_token();
                    if(is_tokenizer_error(peek()))
                    {
                        return raise_parse_error_for_tokenizer_error<int32_t>(
                            peek(), source_pos, indentation_level);
                    }
                    return raise_parse_error<int32_t>(
                        {unexpected_token_message(peek(), source_pos)});

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

        Expected<void> validate_assignment_target(int32_t lhs)
        {
            if(ast.kinds[lhs].node_kind ==
                   AstNodeKind::EXPRESSION_VARIABLE_REFERENCE ||
               ast.kinds[lhs].node_kind == AstNodeKind::EXPRESSION_ATTRIBUTE ||
               (ast.kinds[lhs].node_kind == AstNodeKind::EXPRESSION_BINARY &&
                ast.kinds[lhs].operator_kind == AstOperatorKind::SUBSCRIPT))
            {
                return Expected<void>::ok();
            }

            std::wstring message =
                L"assignment target must be a simple variable, attribute, or "
                L"subscript";
            message += format_error_context(assignment_target_source_pos(lhs));
            return Expected<void>::raise_exception(L"SyntaxError",
                                                   message.c_str());
        }

        uint32_t del_target_source_pos(int32_t node_idx)
        {
            return assignment_target_source_pos(node_idx);
        }

        Expected<void> validate_del_target(int32_t target)
        {
            if(ast.kinds[target].node_kind ==
                   AstNodeKind::EXPRESSION_VARIABLE_REFERENCE ||
               ast.kinds[target].node_kind ==
                   AstNodeKind::EXPRESSION_ATTRIBUTE ||
               (ast.kinds[target].node_kind == AstNodeKind::EXPRESSION_BINARY &&
                ast.kinds[target].operator_kind == AstOperatorKind::SUBSCRIPT))
            {
                return Expected<void>::ok();
            }

            std::wstring message =
                L"del target must be a variable, attribute, or subscript";
            message += format_error_context(del_target_source_pos(target));
            return Expected<void>::raise_exception(L"SyntaxError",
                                                   message.c_str());
        }

        Expected<int32_t> assignment()
        {
            int32_t source_pos = source_pos_for_token();
            bool lhs_parenthesized = peek() == Token::LPAR;
            int32_t lhs = CL_TRY(star_expressions());

            if(match(Token::COLON))
            {
                CL_TRY(validate_assignment_target(lhs));
                int32_t annotation = CL_TRY(expression());
                AstChildren annotation_children{lhs, annotation};
                if(match(Token::EQUAL))
                {
                    annotation_children.push_back(CL_TRY(annotated_rhs()));
                }
                Value simple =
                    !lhs_parenthesized &&
                            ast.kinds[lhs].node_kind ==
                                AstNodeKind::EXPRESSION_VARIABLE_REFERENCE
                        ? Value::True()
                        : Value::False();
                return Expected<int32_t>::ok(
                    ast.emplace_back(AstNodeKind::STATEMENT_ANN_ASSIGN,
                                     source_pos, annotation_children, simple));
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
                    return Expected<int32_t>::ok(ast.emplace_back(
                        AstNodeKind::STATEMENT_EXPRESSION, source_pos, lhs));
            }

            CL_TRY(validate_assignment_target(lhs));
            source_pos = source_pos_and_advance();
            int32_t rhs = CL_TRY(annotated_rhs());
            return Expected<int32_t>::ok(ast.emplace_back(
                AstKind(AstNodeKind::STATEMENT_ASSIGN, op_kind), source_pos,
                lhs, rhs));
        }

        Expected<int32_t> yield_expr()
        {
            return not_implemented(L"yield expression");
        }

        Expected<int32_t> annotated_rhs()
        {
            switch(peek())
            {
                case Token::YIELD:
                    return yield_expr();
                default:
                    return star_expressions();
            }
        }

        Expected<int32_t> return_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::RETURN));
            AstChildren ch;
            if(peek() != Token::NEWLINE && peek() != Token::SEMI)
            {
                ch.push_back(CL_TRY(star_expressions()));
            }
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_RETURN, source_pos, ch));
        }

        Expected<int32_t> raise_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::RAISE));
            if(peek() == Token::NEWLINE || peek() == Token::SEMI ||
               peek() == Token::DEDENT || peek() == Token::ENDMARKER)
            {
                AstChildren ch;
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstNodeKind::STATEMENT_RAISE, source_pos, ch));
            }

            AstChildren ch;
            ch.push_back(CL_TRY(expression()));
            if(peek() == Token::FROM)
            {
                return not_implemented(L"raise from statement");
            }
            return Expected<int32_t>::ok(
                ast.emplace_back(AstNodeKind::STATEMENT_RAISE, source_pos, ch));
        }

        Expected<int32_t> import_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            if(match(Token::FROM))
            {
                int64_t level = 0;
                while(match(Token::DOT))
                {
                    ++level;
                }

                std::wstring module_name;
                uint32_t module_source_pos = source_pos;
                if(peek() != Token::IMPORT)
                {
                    CL_TRY(consume(Token::NAME));
                    module_source_pos = source_pos_for_previous_token();
                    module_name = std::wstring(string_for_name_token(
                        *ast.compilation_unit, module_source_pos));
                    while(match(Token::DOT))
                    {
                        CL_TRY(consume(Token::NAME));
                        uint32_t component_source_pos =
                            source_pos_for_previous_token();
                        module_name += L".";
                        module_name += std::wstring(string_for_name_token(
                            *ast.compilation_unit, component_source_pos));
                    }
                }
                else if(level == 0)
                {
                    CL_TRY(consume(Token::NAME));
                }
                CL_TRY(consume(Token::IMPORT));
                if(peek() == Token::STAR)
                {
                    CL_TRY(consume(Token::STAR));
                    AstChildren targets;
                    targets.push_back(ast.emplace_back(
                        AstNodeKind::EXPRESSION_LITERAL, module_source_pos,
                        Value::from_smi(level)));
                    targets.push_back(ast.emplace_back(
                        AstNodeKind::IMPORT_STAR,
                        source_pos_for_previous_token(), AstChildren{}));
                    return Expected<int32_t>::ok(ast.emplace_back(
                        AstNodeKind::STATEMENT_IMPORT_FROM, source_pos, targets,
                        vm.get_or_create_interned_string_value(module_name)));
                }

                bool parenthesized = match(Token::LPAR);
                AstChildren targets;
                targets.push_back(ast.emplace_back(
                    AstNodeKind::EXPRESSION_LITERAL, module_source_pos,
                    Value::from_smi(level)));
                while(true)
                {
                    CL_TRY(consume(Token::NAME));
                    uint32_t name_source_pos = source_pos_for_previous_token();
                    std::wstring name = std::wstring(string_for_name_token(
                        *ast.compilation_unit, name_source_pos));
                    std::wstring store_name = name;
                    if(match(Token::AS))
                    {
                        CL_TRY(consume(Token::NAME));
                        store_name = std::wstring(string_for_name_token(
                            *ast.compilation_unit,
                            source_pos_for_previous_token()));
                    }
                    TValue<String> store_name_value =
                        vm.get_or_create_interned_string_value(store_name);
                    int32_t target = ast.emplace_back(
                        AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                        name_source_pos, store_name_value);
                    TValue<String> name_value =
                        vm.get_or_create_interned_string_value(name);
                    AstChildren alias_children{target};
                    if(store_name != name)
                    {
                        alias_children.push_back(
                            ast.emplace_back(AstNodeKind::EXPRESSION_LITERAL,
                                             name_source_pos, Value::True()));
                    }
                    targets.push_back(ast.emplace_back(
                        AstNodeKind::IMPORT_ALIAS, name_source_pos,
                        alias_children, name_value));
                    if(!match(Token::COMMA))
                    {
                        break;
                    }
                    while(parenthesized && match(Token::NEWLINE))
                    {
                    }
                    if(parenthesized && peek() == Token::RPAR)
                    {
                        break;
                    }
                    if(!parenthesized &&
                       (peek() == Token::NEWLINE || peek() == Token::SEMI))
                    {
                        break;
                    }
                }
                if(parenthesized)
                {
                    while(match(Token::NEWLINE))
                    {
                    }
                    CL_TRY(consume(Token::RPAR));
                }

                TValue<String> module_name_value =
                    vm.get_or_create_interned_string_value(module_name);
                return Expected<int32_t>::ok(
                    ast.emplace_back(AstNodeKind::STATEMENT_IMPORT_FROM,
                                     source_pos, targets, module_name_value));
            }

            CL_TRY(consume(Token::IMPORT));
            AstChildren aliases;
            while(true)
            {
                CL_TRY(consume(Token::NAME));
                uint32_t name_source_pos = source_pos_for_previous_token();
                std::wstring name = std::wstring(string_for_name_token(
                    *ast.compilation_unit, name_source_pos));
                std::wstring store_name = name;
                bool has_alias = false;
                while(match(Token::DOT))
                {
                    CL_TRY(consume(Token::NAME));
                    uint32_t component_source_pos =
                        source_pos_for_previous_token();
                    name += L".";
                    name += std::wstring(string_for_name_token(
                        *ast.compilation_unit, component_source_pos));
                }

                if(match(Token::AS))
                {
                    has_alias = true;
                    CL_TRY(consume(Token::NAME));
                    store_name = std::wstring(
                        string_for_name_token(*ast.compilation_unit,
                                              source_pos_for_previous_token()));
                }

                TValue<String> store_name_value =
                    vm.get_or_create_interned_string_value(store_name);
                int32_t target =
                    ast.emplace_back(AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                                     name_source_pos, store_name_value);
                TValue<String> full_name_value =
                    vm.get_or_create_interned_string_value(name);
                AstChildren alias_children{target};
                if(has_alias)
                {
                    alias_children.push_back(
                        ast.emplace_back(AstNodeKind::EXPRESSION_LITERAL,
                                         name_source_pos, Value::True()));
                }
                aliases.push_back(
                    ast.emplace_back(AstNodeKind::IMPORT_ALIAS, name_source_pos,
                                     alias_children, full_name_value));

                if(!match(Token::COMMA))
                {
                    break;
                }
                if(peek() == Token::NEWLINE || peek() == Token::SEMI)
                {
                    break;
                }
            }

            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_IMPORT, source_pos, aliases));
        }

        Expected<int32_t> del_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::DEL));
            AstChildren ch;
            ch.push_back(CL_TRY(star_expressions()));
            CL_TRY(validate_del_target(ch.back()));
            while(match(Token::COMMA))
            {
                if(peek() == Token::NEWLINE || peek() == Token::SEMI)
                {
                    break;
                }
                ch.push_back(CL_TRY(star_expressions()));
                CL_TRY(validate_del_target(ch.back()));
            }
            return Expected<int32_t>::ok(
                ast.emplace_back(AstNodeKind::STATEMENT_DEL, source_pos, ch));
        }

        Expected<int32_t> yield_stmt()
        {
            return not_implemented(L"yield statement");
        }

        Expected<int32_t> assert_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::ASSERT));
            AstChildren ch;
            ch.push_back(CL_TRY(expression()));
            if(match(Token::COMMA))
            {
                ch.push_back(CL_TRY(expression()));
            }
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_ASSERT, source_pos, ch));
        }

        Expected<int32_t> break_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::BREAK));
            return Expected<int32_t>::ok(
                ast.emplace_back(AstNodeKind::STATEMENT_BREAK, source_pos, {}));
        }

        Expected<int32_t> continue_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::CONTINUE));
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_CONTINUE, source_pos, {}));
        }

        Expected<int32_t> pass_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::PASS));
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_PASS, source_pos, AstChildren{}));
        }

        Expected<int32_t> global_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::GLOBAL));

            AstChildren ch;
            do
            {
                CL_TRY(consume(Token::NAME));
                uint32_t name_source_pos = source_pos_for_previous_token();
                std::wstring name = std::wstring(string_for_name_token(
                    *ast.compilation_unit, name_source_pos));
                TValue<String> v = vm.get_or_create_interned_string_value(name);
                ch.push_back(
                    ast.emplace_back(AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                                     name_source_pos, v));
            }
            while(match(Token::COMMA));

            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_GLOBAL, source_pos, ch));
        }

        Expected<int32_t> nonlocal_stmt()
        {
            return not_implemented(L"nonlocal statement");
        }

        Expected<int32_t> simple_stmt()
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

        Expected<int32_t> simple_stmts()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            do
            {

                children.emplace_back(CL_TRY(simple_stmt()));
            }
            while(match(Token::SEMI));

            CL_TRY(consume(Token::NEWLINE));

            if(children.size() == 1)
            {
                return Expected<int32_t>::ok(children[0]);
            }
            else
            {
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstNodeKind::STATEMENT_SEQUENCE, source_pos, children));
            }
        }

        Expected<int32_t> function_def()
        {
            if(peek() == Token::AT)
            {
                // todo worry about decorators later
            }
            return function_def_raw();
        }

        Expected<int32_t> function_def_raw()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::DEF));  // todo worry about async later
            CL_TRY(consume(Token::NAME));
            std::wstring name = std::wstring(string_for_name_token(
                *ast.compilation_unit, source_pos_for_previous_token()));
            TValue<String> name_str =
                vm.get_or_create_interned_string_value(name);
            CL_TRY(consume(Token::LPAR));
            int32_t param_seq = CL_TRY(params());
            CL_TRY(consume(Token::RPAR));
            if(peek() == Token::RARROW)
            {
                CL_TRY(consume(Token::RARROW));
                CL_TRY(expression());  // just swallow that return type
                                       // definition, we ignore it
            }

            CL_TRY(consume(Token::COLON));
            // todo worry about func_type_comment later
            int32_t body = CL_TRY(block());
            return Expected<int32_t>::ok(
                ast.emplace_back(AstNodeKind::STATEMENT_FUNCTION_DEF,
                                 source_pos, {param_seq, body}, name_str));
        }

        Expected<int32_t> params() { return parameters(); }

        int32_t parameter_sequence(uint32_t source_pos, AstChildren children)
        {
            return ast.emplace_back(AstNodeKind::PARAMETER_SEQUENCE, source_pos,
                                    children);
        }

        Expected<int32_t> parse_named_parameter(AstNodeKind kind,
                                                bool allow_default)
        {
            CL_TRY(consume(Token::NAME));
            uint32_t name_source_pos = source_pos_for_previous_token();
            std::wstring name = std::wstring(
                string_for_name_token(*ast.compilation_unit, name_source_pos));
            TValue<String> v = vm.get_or_create_interned_string_value(name);
            AstChildren parameter_children;
            if(match(Token::COLON))
            {
                CL_TRY(expression());
            }
            if(match(Token::EQUAL))
            {
                if(!allow_default)
                {
                    if(kind == AstNodeKind::PARAMETER_VARARGS)
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError",
                            L"varargs parameter cannot have a default");
                    }
                    return Expected<int32_t>::raise_exception(
                        L"SyntaxError",
                        L"kwargs parameter cannot have a default");
                }
                parameter_children.push_back(CL_TRY(expression()));
            }
            return Expected<int32_t>::ok(
                ast.emplace_back(kind, name_source_pos, parameter_children, v));
        }

        Expected<int32_t> parameters()
        {
            int32_t source_pos = source_pos_for_token();
            AstChildren posonly;
            AstChildren pos_or_kw;
            AstChildren vararg;
            AstChildren kwonly;
            AstChildren kwarg;
            bool seen_default = false;
            bool seen_slash = false;
            bool parsing_kwonly = false;
            bool seen_star = false;
            bool bare_star_needs_kwonly = false;
            while(peek() != Token::RPAR)
            {
                if(match(Token::SLASH))
                {
                    if(seen_slash || parsing_kwonly || pos_or_kw.empty())
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError",
                            L"invalid positional-only parameter separator");
                    }
                    seen_slash = true;
                    posonly = pos_or_kw;
                    pos_or_kw.clear();
                    if(!match(Token::COMMA))
                    {
                        if(peek() != Token::RPAR)
                        {
                            return Expected<int32_t>::raise_exception(
                                L"SyntaxError",
                                L"positional-only separator must be followed "
                                L"by ',' or ')'");
                        }
                        break;
                    }
                    if(peek() == Token::RPAR)
                    {
                        break;
                    }
                    continue;
                }
                if(match(Token::STAR))
                {
                    if(seen_star)
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError", L"* argument may appear only once");
                    }
                    seen_star = true;
                    parsing_kwonly = true;
                    if(peek() == Token::NAME)
                    {
                        vararg.push_back(CL_TRY(parse_named_parameter(
                            AstNodeKind::PARAMETER_VARARGS, false)));
                    }
                    else if(peek() == Token::RPAR)
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError",
                            L"named arguments must follow bare *");
                    }
                    else
                    {
                        bare_star_needs_kwonly = true;
                    }
                    if(!match(Token::COMMA))
                    {
                        break;
                    }
                    if(peek() == Token::RPAR)
                    {
                        if(vararg.empty())
                        {
                            return Expected<int32_t>::raise_exception(
                                L"SyntaxError",
                                L"named arguments must follow bare *");
                        }
                        break;
                    }
                    continue;
                }

                if(match(Token::DOUBLESTAR))
                {
                    if(bare_star_needs_kwonly)
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError",
                            L"named arguments must follow bare *");
                    }
                    kwarg.push_back(CL_TRY(parse_named_parameter(
                        AstNodeKind::PARAMETER_KWARGS, false)));
                    if(match(Token::COMMA) && peek() != Token::RPAR)
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError",
                            L"arguments cannot follow **kwargs");
                    }
                    break;
                }

                int32_t parameter =
                    CL_TRY(parse_named_parameter(AstNodeKind::PARAMETER, true));
                if(parsing_kwonly)
                {
                    bare_star_needs_kwonly = false;
                    kwonly.push_back(parameter);
                }
                else
                {
                    if(!ast.children[parameter].empty())
                    {
                        seen_default = true;
                    }
                    else if(seen_default)
                    {
                        return Expected<int32_t>::raise_exception(
                            L"SyntaxError",
                            L"non-default argument follows default argument");
                    }
                    pos_or_kw.push_back(parameter);
                }

                if(!match(Token::COMMA))
                {
                    break;
                }
                if(peek() == Token::RPAR)
                {
                    break;
                }
            }

            AstChildren signature_children;
            signature_children.push_back(
                parameter_sequence(source_pos, posonly));
            signature_children.push_back(
                parameter_sequence(source_pos, pos_or_kw));
            signature_children.push_back(
                parameter_sequence(source_pos, vararg));
            signature_children.push_back(
                parameter_sequence(source_pos, kwonly));
            signature_children.push_back(parameter_sequence(source_pos, kwarg));
            return Expected<int32_t>::ok(
                ast.emplace_back(AstNodeKind::PARAMETER_SIGNATURE, source_pos,
                                 signature_children));
        }

        Expected<int32_t> if_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::IF));
            children.push_back(CL_TRY(named_expression()));
            CL_TRY(consume(Token::COLON));
            children.push_back(CL_TRY(block()));
            while(peek() == Token::ELIF)
            {
                CL_TRY(consume(Token::ELIF));
                children.push_back(CL_TRY(named_expression()));
                CL_TRY(consume(Token::COLON));
                children.push_back(CL_TRY(block()));
            }
            if(peek() == Token::ELSE)
            {
                CL_TRY(consume(Token::ELSE));
                CL_TRY(consume(Token::COLON));
                children.push_back(CL_TRY(block()));
            }
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_IF, source_pos, children));
        }

        Expected<int32_t> class_def()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::CLASS));
            CL_TRY(consume(Token::NAME));
            std::wstring name = std::wstring(string_for_name_token(
                *ast.compilation_unit, source_pos_for_previous_token()));
            TValue<String> name_str =
                vm.get_or_create_interned_string_value(name);

            AstChildren children;
            int32_t bases = ast.emplace_back(AstNodeKind::PARAMETER_SEQUENCE,
                                             source_pos, AstChildren{});
            if(match(Token::LPAR))
            {
                bases = CL_TRY(args());
                CL_TRY(consume(Token::RPAR));
            }
            children.push_back(bases);

            CL_TRY(consume(Token::COLON));
            children.push_back(CL_TRY(block()));
            return Expected<int32_t>::ok(
                ast.emplace_back(AstNodeKind::STATEMENT_CLASS_DEF, source_pos,
                                 children, name_str));
        }

        Expected<int32_t> with_item()
        {
            int32_t source_pos = source_pos_for_token();
            AstChildren children;
            children.push_back(CL_TRY(named_expression()));
            if(match(Token::AS))
            {
                int32_t target = CL_TRY(star_expression());
                CL_TRY(validate_assignment_target(target));
                children.push_back(target);
            }
            return Expected<int32_t>::ok(
                ast.emplace_back(AstNodeKind::WITH_ITEM, source_pos, children));
        }

        Expected<int32_t> with_stmt()
        {
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::WITH));
            AstChildren children;
            children.push_back(CL_TRY(with_item()));
            while(match(Token::COMMA))
            {
                children.push_back(CL_TRY(with_item()));
            }
            CL_TRY(consume(Token::COLON));
            children.push_back(CL_TRY(block()));
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_WITH, source_pos, children));
        }

        Expected<int32_t> for_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::FOR));
            uint32_t target_start_pos = source_pos_for_token();
            int32_t target = -1;
            if(peek() == Token::NAME)
            {
                std::wstring name = std::wstring(string_for_name_token(
                    *ast.compilation_unit, source_pos_for_token()));
                TValue<String> v = vm.get_or_create_interned_string_value(name);
                target =
                    ast.emplace_back(AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                                     source_pos_and_advance(), v);
                if(peek() == Token::COMMA)
                {
                    AstChildren target_children =
                        CL_TRY(sequence_until_stop_token(
                            target, &Parser::star_expression, Token::COMMA,
                            Token::IN));
                    target =
                        ast.emplace_back(AstNodeKind::EXPRESSION_TUPLE,
                                         target_start_pos, target_children);
                }
            }
            else
            {
                target = CL_TRY(star_expressions());
            }
            CL_TRY(validate_assignment_target(target));
            children.push_back(target);
            CL_TRY(consume(Token::IN));
            children.push_back(CL_TRY(named_expression()));
            CL_TRY(consume(Token::COLON));
            children.push_back(CL_TRY(block()));

            if(peek() == Token::ELSE)
            {
                CL_TRY(consume(Token::ELSE));
                CL_TRY(consume(Token::COLON));
                children.push_back(CL_TRY(block()));
            }
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_FOR, source_pos, children));
        }

        Expected<int32_t> try_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::TRY));
            CL_TRY(consume(Token::COLON));
            children.push_back(CL_TRY(block()));

            if(peek() == Token::FINALLY)
            {
                int32_t finally_source_pos = source_pos_for_token();
                CL_TRY(consume(Token::FINALLY));
                CL_TRY(consume(Token::COLON));
                AstChildren finally_children;
                finally_children.push_back(CL_TRY(block()));
                children.push_back(
                    ast.emplace_back(AstNodeKind::STATEMENT_FINALLY_HANDLER,
                                     finally_source_pos, finally_children));
                return Expected<int32_t>::ok(ast.emplace_back(
                    AstNodeKind::STATEMENT_TRY, source_pos, children));
            }

            if(peek() != Token::EXCEPT)
            {
                return not_implemented(L"try statement without except");
            }

            bool saw_bare_except = false;
            while(peek() == Token::EXCEPT)
            {
                if(saw_bare_except)
                {
                    return not_implemented(L"except after bare except");
                }
                int32_t handler_source_pos = source_pos_for_token();
                CL_TRY(consume(Token::EXCEPT));
                AstChildren handler_children;
                if(peek() != Token::COLON)
                {
                    handler_children.push_back(CL_TRY(expression()));
                    if(match(Token::AS))
                    {
                        CL_TRY(consume(Token::NAME));
                        uint32_t name_source_pos =
                            source_pos_for_previous_token();
                        std::wstring name = std::wstring(string_for_name_token(
                            *ast.compilation_unit, name_source_pos));
                        TValue<String> v =
                            vm.get_or_create_interned_string_value(name);
                        handler_children.push_back(ast.emplace_back(
                            AstNodeKind::EXPRESSION_VARIABLE_REFERENCE,
                            name_source_pos, v));
                    }
                }
                else
                {
                    saw_bare_except = true;
                }
                CL_TRY(consume(Token::COLON));
                handler_children.push_back(CL_TRY(block()));
                children.push_back(
                    ast.emplace_back(AstNodeKind::STATEMENT_EXCEPT_HANDLER,
                                     handler_source_pos, handler_children));
            }
            if(peek() == Token::ELSE)
            {
                int32_t else_source_pos = source_pos_for_token();
                CL_TRY(consume(Token::ELSE));
                CL_TRY(consume(Token::COLON));
                AstChildren else_children;
                else_children.push_back(CL_TRY(block()));
                children.push_back(
                    ast.emplace_back(AstNodeKind::STATEMENT_ELSE_HANDLER,
                                     else_source_pos, else_children));
            }
            if(peek() == Token::FINALLY)
            {
                int32_t finally_source_pos = source_pos_for_token();
                CL_TRY(consume(Token::FINALLY));
                CL_TRY(consume(Token::COLON));
                AstChildren finally_children;
                finally_children.push_back(CL_TRY(block()));
                children.push_back(
                    ast.emplace_back(AstNodeKind::STATEMENT_FINALLY_HANDLER,
                                     finally_source_pos, finally_children));
            }

            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_TRY, source_pos, children));
        }

        Expected<int32_t> while_stmt()
        {
            AstChildren children;
            int32_t source_pos = source_pos_for_token();
            CL_TRY(consume(Token::WHILE));
            children.push_back(CL_TRY(named_expression()));
            CL_TRY(consume(Token::COLON));
            children.push_back(CL_TRY(block()));

            if(peek() == Token::ELSE)
            {
                CL_TRY(consume(Token::ELSE));
                CL_TRY(consume(Token::COLON));
                children.push_back(CL_TRY(block()));
            }
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_WHILE, source_pos, children));
        }

        Expected<int32_t> match_stmt()
        {
            return not_implemented(L"match statement");
        }

        Expected<int32_t> compound_statement()
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

        Expected<int32_t> statement()
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

        Expected<int32_t> statements()
        {
            int32_t source_pos = source_pos_for_token();
            AstChildren children;
            do
            {
                children.push_back(CL_TRY(statement()));
            }
            while(peek() != Token::DEDENT && peek() != Token::ENDMARKER);
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_SEQUENCE, source_pos, children));
        }

        Expected<int32_t> file()
        {
            while(match(Token::NEWLINE))
            {
            }
            int32_t source_pos = source_pos_for_token();
            int32_t idx = -1;
            if(!is_at_end())
            {
                idx = CL_TRY(statements());
            }
            else
            {
                idx = ast.emplace_back(AstNodeKind::STATEMENT_SEQUENCE,
                                       source_pos, AstChildren{});
            }
            CL_TRY(consume(Token::ENDMARKER));
            return Expected<int32_t>::ok(idx);
        }

        Expected<int32_t> interactive()
        {
            while(match(Token::NEWLINE))
            {
            }
            int32_t source_pos = source_pos_for_token();
            AstChildren children;
            if(!is_at_end())
            {
                children.push_back(CL_TRY(statement()));
            }
            while(match(Token::NEWLINE))
            {
            }
            CL_TRY(consume(Token::ENDMARKER));
            return Expected<int32_t>::ok(ast.emplace_back(
                AstNodeKind::STATEMENT_SEQUENCE, source_pos, children));
        }

        Expected<int32_t> eval()
        {
            int32_t result = CL_TRY(expressions());
            while(match(Token::NEWLINE))
            {
            }

            CL_TRY(consume(Token::ENDMARKER));
            return Expected<int32_t>::ok(result);
        }
    };

    Expected<AstVector>
    parse(VirtualMachine &vm, const TokenVector &tv, StartRule start_rule,
          CompileContinuationInfo *compile_continuation_info)
    {
        Parser parser(vm, tv, compile_continuation_info);
        return parser.parse(start_rule);
    }

}  // namespace cl
