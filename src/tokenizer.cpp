#include "tokenizer.h"
#include "compilation_unit.h"
#include "token.h"
#include <limits>
#include <algorithm>
#include <absl/container/flat_hash_map.h>
#include <string_view>


namespace cl
{


    static absl::flat_hash_map<std::wstring_view, Token> make_keyword_token_map()
    {
        using namespace std::literals;

        absl::flat_hash_map<std::wstring_view, Token> keywords =
        {
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



    void tokenise(CompilationUnit &cu)
    {
        const std::wstring &source_code = cu.source_code;
        TokenVector &tokens = cu.tokens;

        absl::flat_hash_map<std::wstring_view, Token> keywords = make_keyword_token_map();

        std::vector<uint32_t> indents;
        indents.push_back(0);
        if(source_code.size() > std::numeric_limits<uint32_t>::max())
        {
            throw std::runtime_error("Too large file");
        }
        uint32_t pos = 0;
        uint32_t max = source_code.size();
        static constexpr uint32_t tabsize = 8;

        enum {
            START_LINE,
            NORMAL,
            IN_PAREN,
            CONTINUED,
            CONTSTR
        } state = START_LINE;


        while(pos < max)
        {
            switch(state)
            {
            case START_LINE:
            {
                uint32_t column = 0;
                while(pos < max)
                {
                    wchar_t c = source_code[pos];
                    if(c == ' ')
                    {
                        ++column;
                    } else if(c == '\t')
                    {
                        column = (column/tabsize + 1)*tabsize;
                    } else if(c == '\f')
                    {
                        column = 0;
                    } else {
                        break;
                    }
                    ++pos;


                }
                if(pos == max) break;

                wchar_t c = source_code[pos];
                if(c == '#' || c == '\r' || c == '\n')
                {
                    // comments or blank lines don't count for the indentation algorithm
                    break;
                }

                if(column > indents.back())
                {
                    indents.push_back(column);
                    tokens.emplace_back(Token::INDENT, pos);
                }
                if(column < indents.back())
                {
                    if(std::find(indents.begin(), indents.end(), pos) == indents.end())
                    {
                        throw std::runtime_error("IndentationError: unindent does not match any outer indentation level " + std::to_string(pos));
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
                wchar_t c2 = pos+1 < max ? source_code[pos+1] : 0;
                wchar_t c3 = pos+2 < max ? source_code[pos+2] : 0;
                bool c2_equal = (c2 == '=');
                switch(c)
                {
                case '(':
                    tokens.emplace_back(Token::LPAR, pos++);
                    break;
                case ')':
                    tokens.emplace_back(Token::RPAR, pos++);
                    break;
                case '[':
                    tokens.emplace_back(Token::LSQB, pos++);
                    break;
                case ']':
                    tokens.emplace_back(Token::RSQB, pos++);
                    break;

                case ':':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::COLONEQUAL, pos);
                        pos += 2;
                    } else {
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
                    } else {
                        tokens.emplace_back(Token::PLUS, pos++);
                    }
                    break;
                case '-':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::MINEQUAL, pos);
                        pos += 2;
                    } else if(c2 == '>')
                    {
                        tokens.emplace_back(Token::RARROW, pos);
                        pos += 2;
                    } else {
                        tokens.emplace_back(Token::MINUS, pos++);
                    }
                    break;
                case '*':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::STAREQUAL, pos);
                        pos += 2;
                    } else if(c2 == '*')
                    {
                        if(c3 == '=')
                        {
                            tokens.emplace_back(Token::DOUBLESTAREQUAL, pos);
                            pos += 3;
                        } else {
                            tokens.emplace_back(Token::DOUBLESTAR, pos);
                            pos += 2;
                        }
                    } else {
                        tokens.emplace_back(Token::STAR, pos++);
                    }
                    break;
                case '/':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::SLASHEQUAL, pos);
                        pos += 2;
                    } else if(c2 == '/')
                    {
                        if(c3 == '=')
                        {
                            tokens.emplace_back(Token::DOUBLESLASHEQUAL, pos);
                            pos += 3;
                        } else {
                            tokens.emplace_back(Token::DOUBLESLASH, pos);
                            pos += 2;
                        }
                    } else {
                        tokens.emplace_back(Token::SLASH, pos++);
                    }
                    break;
                case '|':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::VBAREQUAL, pos);
                        pos += 2;
                    } else {
                        tokens.emplace_back(Token::VBAR, pos++);
                    }
                    break;
                case '&':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::AMPEREQUAL, pos);
                        pos += 2;
                    } else {
                        tokens.emplace_back(Token::AMPER, pos++);
                    }
                    break;

                case '<':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::LESSEQUAL, pos);
                        pos += 2;
                    } else if(c2 == '<')
                    {
                        if(c3 == '=')
                        {
                            tokens.emplace_back(Token::LEFTSHIFTEQUAL, pos);
                            pos += 3;
                        } else {
                            tokens.emplace_back(Token::LEFTSHIFT, pos);
                            pos += 2;
                        }
                    } else {
                        tokens.emplace_back(Token::LESS, pos++);
                    }
                    break;
                case '>':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::GREATEREQUAL, pos);
                        pos += 2;
                    } else if(c2 == '>')
                    {
                        if(c3 == '=')
                        {
                            tokens.emplace_back(Token::RIGHTSHIFTEQUAL, pos);
                            pos += 3;
                        } else {
                            tokens.emplace_back(Token::RIGHTSHIFT, pos);
                            pos += 2;
                        }
                    } else {
                        tokens.emplace_back(Token::GREATER, pos++);
                    }
                    break;

                case '=':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::EQEQUAL, pos);
                        pos += 2;
                    } else {
                        tokens.emplace_back(Token::EQUAL, pos++);
                    }
                    break;

                case '.':
                    if(c2 == '.' && c3 == '.')
                    {
                        tokens.emplace_back(Token::ELLIPSIS, pos);
                        pos += 3;
                    } else {
                        tokens.emplace_back(Token::DOT, pos++);
                    }
                    break;


                case '%':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::PERCENTEQUAL, pos);
                        pos += 2;
                    } else {
                        tokens.emplace_back(Token::PERCENT, pos++);
                    }
                    break;

                case '{':
                    tokens.emplace_back(Token::LBRACE, pos++);
                    break;
                case '}':
                    tokens.emplace_back(Token::RBRACE, pos++);
                    break;

                case '!':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::NOTEQUAL, pos);
                        pos += 2;
                    } else {

                        tokens.emplace_back(Token::ERRORTOKEN, pos++);
                    }
                    break;

                case '~':
                    tokens.emplace_back(Token::TILDE, pos++);
                    break;

                case '^':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::CIRCUMFLEXEQUAL, pos);
                        pos += 2;
                    } else {
                        tokens.emplace_back(Token::CIRCUMFLEX, pos++);
                    }
                    break;

                case '@':
                    if(c2_equal)
                    {
                        tokens.emplace_back(Token::ATEQUAL, pos);
                        pos += 2;
                    } else {
                        tokens.emplace_back(Token::AT, pos++);
                    }
                    break;


                }
                break;
            }


            }


        }


        tokens.emplace_back(Token::ENDMARKER, max);

    }



}
