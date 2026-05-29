#include "tokenizer.h"
#include "compilation_unit.h"
#include "token.h"
#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <ctre-unicode.hpp>
#include <limits>
#include <string_view>

namespace cl
{

    static constexpr auto name_pattern = ctll::fixed_string{"^\\w+"};
    static constexpr auto name_re = ctre::re<name_pattern>();

    static constexpr auto int_number_pattern =
        ctll::fixed_string{"^("
                           "(0[xX](?:_?[0-9a-fA-F])+)"  // hexadecimal
                           "|"
                           "(0[bB](?:_?[01])+)"  // binary
                           "|"
                           "(0[oO](?:_?[0-7])+)"  // octal
                           "|"
                           "(?:0(?:_?0)*|[1-9](?:_?[0-9])*)"
                           ")"};
    static constexpr auto int_number_re = ctre::re<int_number_pattern>();

    static constexpr auto float_number_pattern = ctll::fixed_string{
        "^("
        "((?:[0-9](?:_?[0-9])*)?\\.[0-9](?:_?[0-9])*|"
        "[0-9](?:_?[0-9])*\\.)(?:[eE](?:\\+|-)?[0-9](?:_?[0-9])*)?"
        "|"
        "[0-9](?:_?[0-9])*[eE](?:\\+|-)?[0-9](?:_?[0-9])*)"};
    static constexpr auto float_number_re = ctre::re<float_number_pattern>();

    static constexpr auto string_prefix_pattern =
        ctll::fixed_string{"^[rRuU]{0,2}"};
    static constexpr auto string_prefix_re = ctre::re<string_prefix_pattern>();

    static bool is_string_prefix(std::wstring_view prefix)
    {
        if(prefix.empty())
        {
            return true;
        }
        if(prefix.size() > 2)
        {
            return false;
        }

        bool seen_r = false;
        bool seen_u = false;
        for(wchar_t c: prefix)
        {
            if(c == L'r' || c == L'R')
            {
                if(seen_r)
                {
                    return false;
                }
                seen_r = true;
            }
            else if(c == L'u' || c == L'U')
            {
                if(seen_u)
                {
                    return false;
                }
                seen_u = true;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    enum class StringTokenKind
    {
        NotString,
        Complete,
        UnterminatedString,
        UnterminatedTripleString,
    };

    struct StringTokenScan
    {
        StringTokenKind kind;
        size_t length;
    };

    static StringTokenScan scan_string_literal(std::wstring_view s)
    {
        if(s.empty())
        {
            return {StringTokenKind::NotString, 0};
        }

        auto prefix_match = string_prefix_re.search(s);
        size_t prefix_len = 0;
        if(prefix_match)
        {
            prefix_len = std::wstring_view(prefix_match).size();
        }
        if(!is_string_prefix(s.substr(0, prefix_len)))
        {
            return {StringTokenKind::NotString, 0};
        }

        if(prefix_len >= s.size())
        {
            return {StringTokenKind::NotString, 0};
        }

        wchar_t quote = s[prefix_len];
        if(quote != L'\'' && quote != L'"')
        {
            return {StringTokenKind::NotString, 0};
        }

        size_t quote_len = 1;
        if(prefix_len + 2 < s.size() && s[prefix_len + 1] == quote &&
           s[prefix_len + 2] == quote)
        {
            quote_len = 3;
        }

        bool escaped = false;
        for(size_t i = prefix_len + quote_len; i < s.size(); ++i)
        {
            wchar_t c = s[i];
            if(quote_len == 1 && (c == L'\n' || c == L'\r'))
            {
                return {StringTokenKind::UnterminatedString, i};
            }

            if(escaped)
            {
                escaped = false;
                continue;
            }

            if(c == L'\\')
            {
                escaped = true;
                continue;
            }

            if(c == quote)
            {
                if(quote_len == 1)
                {
                    return {StringTokenKind::Complete, i + 1};
                }
                if(i + 2 < s.size() && s[i + 1] == quote && s[i + 2] == quote)
                {
                    return {StringTokenKind::Complete, i + 3};
                }
            }
        }

        if(quote_len == 3)
        {
            return {StringTokenKind::UnterminatedTripleString, s.size()};
        }
        return {StringTokenKind::UnterminatedString, s.size()};
    }

    static size_t find_string_literal_length(std::wstring_view s)
    {
        StringTokenScan scan = scan_string_literal(s);
        if(scan.kind == StringTokenKind::Complete)
        {
            return scan.length;
        }
        return 0;
    }

    std::wstring_view string_for_name_token(const CompilationUnit &cu,
                                            uint32_t offset)
    {
        std::wstring_view s = cu.get_source_view().substr(offset);
        auto m = name_re.search(s);
        if(m)
        {
            return std::wstring_view(m);
        }
        return std::wstring_view();
    }

    std::wstring_view string_for_int_number_token(const CompilationUnit &cu,
                                                  uint32_t offset)
    {
        std::wstring_view s = cu.get_source_view().substr(offset);
        auto m = int_number_re.search(s);
        if(m)
        {
            return std::wstring_view(m);
        }
        return std::wstring_view();
    }

    std::wstring_view string_for_float_number_token(const CompilationUnit &cu,
                                                    uint32_t offset)
    {
        std::wstring_view s = cu.get_source_view().substr(offset);
        auto m = float_number_re.search(s);
        if(m)
        {
            return std::wstring_view(m);
        }
        return std::wstring_view();
    }

    std::wstring_view string_for_number_token(const CompilationUnit &cu,
                                              uint32_t offset)
    {
        std::wstring_view m = string_for_float_number_token(cu, offset);
        if(!m.empty())
        {
            return m;
        }
        return string_for_int_number_token(cu, offset);
    }

    std::wstring_view string_for_string_token(const CompilationUnit &cu,
                                              uint32_t offset)
    {
        std::wstring_view s = cu.get_source_view().substr(offset);
        size_t literal_len = find_string_literal_length(s);
        if(literal_len > 0)
        {
            return s.substr(0, literal_len);
        }
        return std::wstring_view();
    }

    static absl::flat_hash_map<std::wstring_view, Token>
    make_keyword_token_map()
    {
        using namespace std::literals;

        absl::flat_hash_map<std::wstring_view, Token> keywords = {
            {L"False"sv, Token::FALSE},
            {L"None"sv, Token::NONE},
            {L"True"sv, Token::TRUE},

            {L"and"sv, Token::AND},
            {L"as"sv, Token::AS},
            {L"assert"sv, Token::ASSERT},
            {L"await"sv, Token::AWAIT},
            {L"async"sv, Token::ASYNC},
            {L"break"sv, Token::BREAK},
            {L"class"sv, Token::CLASS},
            {L"continue"sv, Token::CONTINUE},
            {L"def"sv, Token::DEF},
            {L"del"sv, Token::DEL},
            {L"elif"sv, Token::ELIF},
            {L"else"sv, Token::ELSE},
            {L"except"sv, Token::EXCEPT},
            {L"finally"sv, Token::FINALLY},
            {L"for"sv, Token::FOR},
            {L"from"sv, Token::FROM},
            {L"global"sv, Token::GLOBAL},
            {L"if"sv, Token::IF},
            {L"import"sv, Token::IMPORT},
            {L"in"sv, Token::IN},
            {L"is"sv, Token::IS},
            {L"lambda"sv, Token::LAMBDA},
            {L"nonlocal"sv, Token::NONLOCAL},
            {L"not"sv, Token::NOT},
            {L"or"sv, Token::OR},
            {L"pass"sv, Token::PASS},
            {L"raise"sv, Token::RAISE},
            {L"return"sv, Token::RETURN},
            {L"try"sv, Token::TRY},
            {L"while"sv, Token::WHILE},
            {L"with"sv, Token::WITH},
            {L"yield"sv, Token::YIELD},
        };
        return keywords;
    }

    Expected<TokenVector> tokenize(CompilationUnit &cu)
    {
        const std::wstring &source_code = cu.source_code;
        TokenVector tokens(&cu);

        absl::flat_hash_map<std::wstring_view, Token> keywords =
            make_keyword_token_map();

        std::vector<uint32_t> indents;
        indents.push_back(0);
        if(source_code.size() > std::numeric_limits<uint32_t>::max())
        {
            return Expected<TokenVector>::raise_exception(
                L"SyntaxError", L"source file too large");
        }
        uint32_t pos = 0;
        uint32_t end = source_code.size();
        uint32_t bracket_depth = 0;
        static constexpr uint32_t tabsize = 8;

        enum
        {
            START_LINE,
            NORMAL,
            // IN_PAREN,
            // CONTINUED,
            // CONTSTR
        } state = START_LINE;

        while(pos < end)
        {
            switch(state)
            {
                case START_LINE:
                    {
                        uint32_t column = 0;
                        while(pos < end)
                        {
                            wchar_t c = source_code[pos];
                            if(c == ' ')
                            {
                                ++column;
                            }
                            else if(c == '\t')
                            {
                                column = (column / tabsize + 1) * tabsize;
                            }
                            else if(c == '\f')
                            {
                                column = 0;
                            }
                            else
                            {
                                break;
                            }
                            ++pos;
                        }
                        if(pos == end)
                            break;

                        wchar_t c = source_code[pos];
                        if(c == '#' || c == '\r' || c == '\n')
                        {
                            // comments or blank lines don't count for the
                            // indentation algorithm
                            state = NORMAL;
                            break;
                        }

                        if(column > indents.back())
                        {
                            indents.push_back(column);
                            tokens.emplace_back(Token::INDENT, pos);
                        }

                        if(column < indents.back())
                        {
                            if(std::find(indents.begin(), indents.end(),
                                         column) == indents.end())
                            {
                                std::wstring message =
                                    L"unindent does not match any outer "
                                    L"indentation level " +
                                    std::to_wstring(column);
                                return Expected<TokenVector>::raise_exception(
                                    L"IndentationError", message.c_str());
                            }

                            while(column < indents.back())
                            {
                                indents.pop_back();
                                tokens.emplace_back(Token::DEDENT, pos);
                            }
                        }

                        state = NORMAL;

                        break;
                    }
                case NORMAL:
                    {
                        wchar_t c = source_code[pos];
                        wchar_t c2 = pos + 1 < end ? source_code[pos + 1] : 0;
                        wchar_t c3 = pos + 2 < end ? source_code[pos + 2] : 0;
                        bool c2_equal = (c2 == '=');
                        switch(c)
                        {
                            case '(':
                                ++bracket_depth;
                                tokens.emplace_back(Token::LPAR, pos++);
                                break;
                            case ')':
                                if(bracket_depth > 0)
                                {
                                    --bracket_depth;
                                }
                                tokens.emplace_back(Token::RPAR, pos++);
                                break;
                            case '[':
                                ++bracket_depth;
                                tokens.emplace_back(Token::LSQB, pos++);
                                break;
                            case ']':
                                if(bracket_depth > 0)
                                {
                                    --bracket_depth;
                                }
                                tokens.emplace_back(Token::RSQB, pos++);
                                break;

                            case ':':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::COLONEQUAL, pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::COLON, pos++);
                                }
                                break;

                            case ',':
                                tokens.emplace_back(Token::COMMA, pos++);
                                break;
                            case ';':
                                tokens.emplace_back(Token::SEMI, pos++);
                                break;

                            case '+':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::PLUSEQUAL, pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::PLUS, pos++);
                                }
                                break;
                            case '-':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::MINEQUAL, pos);
                                    pos += 2;
                                }
                                else if(c2 == '>')
                                {
                                    tokens.emplace_back(Token::RARROW, pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::MINUS, pos++);
                                }
                                break;
                            case '*':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::STAREQUAL, pos);
                                    pos += 2;
                                }
                                else if(c2 == '*')
                                {
                                    if(c3 == '=')
                                    {
                                        tokens.emplace_back(
                                            Token::DOUBLESTAREQUAL, pos);
                                        pos += 3;
                                    }
                                    else
                                    {
                                        tokens.emplace_back(Token::DOUBLESTAR,
                                                            pos);
                                        pos += 2;
                                    }
                                }
                                else
                                {
                                    tokens.emplace_back(Token::STAR, pos++);
                                }
                                break;
                            case '/':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::SLASHEQUAL, pos);
                                    pos += 2;
                                }
                                else if(c2 == '/')
                                {
                                    if(c3 == '=')
                                    {
                                        tokens.emplace_back(
                                            Token::DOUBLESLASHEQUAL, pos);
                                        pos += 3;
                                    }
                                    else
                                    {
                                        tokens.emplace_back(Token::DOUBLESLASH,
                                                            pos);
                                        pos += 2;
                                    }
                                }
                                else
                                {
                                    tokens.emplace_back(Token::SLASH, pos++);
                                }
                                break;
                            case '|':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::VBAREQUAL, pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::VBAR, pos++);
                                }
                                break;
                            case '&':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::AMPEREQUAL, pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::AMPER, pos++);
                                }
                                break;

                            case '<':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::LESSEQUAL, pos);
                                    pos += 2;
                                }
                                else if(c2 == '<')
                                {
                                    if(c3 == '=')
                                    {
                                        tokens.emplace_back(
                                            Token::LEFTSHIFTEQUAL, pos);
                                        pos += 3;
                                    }
                                    else
                                    {
                                        tokens.emplace_back(Token::LEFTSHIFT,
                                                            pos);
                                        pos += 2;
                                    }
                                }
                                else
                                {
                                    tokens.emplace_back(Token::LESS, pos++);
                                }
                                break;
                            case '>':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::GREATEREQUAL,
                                                        pos);
                                    pos += 2;
                                }
                                else if(c2 == '>')
                                {
                                    if(c3 == '=')
                                    {
                                        tokens.emplace_back(
                                            Token::RIGHTSHIFTEQUAL, pos);
                                        pos += 3;
                                    }
                                    else
                                    {
                                        tokens.emplace_back(Token::RIGHTSHIFT,
                                                            pos);
                                        pos += 2;
                                    }
                                }
                                else
                                {
                                    tokens.emplace_back(Token::GREATER, pos++);
                                }
                                break;

                            case '=':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::EQEQUAL, pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::EQUAL, pos++);
                                }
                                break;

                            case '.':
                                if(c2 == '.' && c3 == '.')
                                {
                                    tokens.emplace_back(Token::ELLIPSIS, pos);
                                    pos += 3;
                                }
                                else if(c2 >= L'0' && c2 <= L'9')
                                {
                                    std::wstring_view m =
                                        string_for_float_number_token(cu, pos);
                                    tokens.emplace_back(Token::FLOAT_NUMBER,
                                                        pos);
                                    pos += m.size();
                                }
                                else
                                {
                                    tokens.emplace_back(Token::DOT, pos++);
                                }
                                break;

                            case '%':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::PERCENTEQUAL,
                                                        pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::PERCENT, pos++);
                                }
                                break;

                            case '{':
                                ++bracket_depth;
                                tokens.emplace_back(Token::LBRACE, pos++);
                                break;
                            case '}':
                                if(bracket_depth > 0)
                                {
                                    --bracket_depth;
                                }
                                tokens.emplace_back(Token::RBRACE, pos++);
                                break;

                            case '!':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::NOTEQUAL, pos);
                                    pos += 2;
                                }
                                else
                                {

                                    tokens.emplace_back(Token::EXCLAMATION,
                                                        pos++);
                                }
                                break;

                            case '~':
                                tokens.emplace_back(Token::TILDE, pos++);
                                break;

                            case '^':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::CIRCUMFLEXEQUAL,
                                                        pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::CIRCUMFLEX,
                                                        pos++);
                                }
                                break;

                            case '@':
                                if(c2_equal)
                                {
                                    tokens.emplace_back(Token::ATEQUAL, pos);
                                    pos += 2;
                                }
                                else
                                {
                                    tokens.emplace_back(Token::AT, pos++);
                                }
                                break;
                            case '\n':
                            case '\r':
                                if(bracket_depth > 0)
                                {
                                    while(pos < end &&
                                          (source_code[pos] == '\n' ||
                                           source_code[pos] == '\r'))
                                    {
                                        ++pos;
                                    }
                                    break;
                                }
                                if(tokens.size() == 0 ||
                                   tokens.tokens.back() != Token::NEWLINE)
                                {
                                    tokens.emplace_back(Token::NEWLINE, pos);
                                }
                                while(pos < end && (source_code[pos] == '\n' ||
                                                    source_code[pos] == '\r'))
                                {
                                    ++pos;
                                }
                                state = START_LINE;
                                break;

                            case ' ':
                            case '\f':
                            case '\t':
                                // skip over whitespace
                                ++pos;
                                break;

                            case '#':
                                // skip over comments until end of line
                                do
                                {
                                    ++pos;
                                }
                                while(pos < end && source_code[pos] != '\n' &&
                                      source_code[pos] != '\r');
                                break;

                            default:
                                {
                                    {
                                        StringTokenScan scan =
                                            scan_string_literal(
                                                std::wstring_view(source_code)
                                                    .substr(pos));
                                        if(scan.kind ==
                                           StringTokenKind::Complete)
                                        {
                                            tokens.emplace_back(Token::STRING,
                                                                pos);
                                            pos += scan.length;
                                            break;
                                        }
                                        if(scan.kind ==
                                           StringTokenKind::UnterminatedString)
                                        {
                                            tokens.emplace_back(
                                                Token::
                                                    ERRORTOKEN_UNTERMINATED_STRING,
                                                pos);
                                            pos += std::max<size_t>(scan.length,
                                                                    1);
                                            break;
                                        }
                                        if(scan.kind ==
                                           StringTokenKind::
                                               UnterminatedTripleString)
                                        {
                                            tokens.emplace_back(
                                                Token::
                                                    ERRORTOKEN_UNTERMINATED_TRIPLE_STRING,
                                                pos);
                                            pos += std::max<size_t>(scan.length,
                                                                    1);
                                            break;
                                        }
                                    }

                                    {
                                        std::wstring_view m =
                                            string_for_float_number_token(cu,
                                                                          pos);
                                        if(!m.empty())
                                        {
                                            tokens.emplace_back(
                                                Token::FLOAT_NUMBER, pos);
                                            pos += m.size();
                                            break;
                                        }
                                    }

                                    {
                                        std::wstring_view m =
                                            string_for_int_number_token(cu,
                                                                        pos);
                                        if(!m.empty())
                                        {
                                            tokens.emplace_back(
                                                Token::INT_NUMBER, pos);
                                            pos += m.size();
                                            break;
                                        }
                                    }

                                    {
                                        std::wstring_view m =
                                            string_for_name_token(cu, pos);
                                        if(!m.empty())
                                        {
                                            Token t = Token::NAME;
                                            auto it = keywords.find(m);
                                            if(it != keywords.end())
                                            {
                                                t = it->second;
                                            }
                                            tokens.emplace_back(t, pos);
                                            pos += m.size();
                                            break;
                                        }
                                    }

                                    tokens.emplace_back(
                                        Token::ERRORTOKEN_INVALID_CHARACTER,
                                        pos++);
                                }
                        }
                        break;
                    }
            }
        }

        if(bracket_depth > 0)
        {
            tokens.emplace_back(Token::ERRORTOKEN_OPEN_BRACKET_EOF, end);
        }
        else if(tokens.size() == 0 || tokens.tokens.back() != Token::NEWLINE)
        {
            // terminate with a newline if not there already
            tokens.emplace_back(Token::NEWLINE, end);
        }
        while(indents.size() > 1)
        {
            indents.pop_back();
            tokens.emplace_back(Token::DEDENT, end);
        }

        tokens.emplace_back(Token::ENDMARKER, end);

        return Expected<TokenVector>::ok(std::move(tokens));
    }

}  // namespace cl
