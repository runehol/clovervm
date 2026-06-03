#ifndef CL_TOKEN_H
#define CL_TOKEN_H

#include <cassert>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace cl
{

    struct CompilationUnit;

    enum class Token : uint8_t
    {
        // end of file
        ENDMARKER,

        // metaclasses
        NAME,
        INT_NUMBER,
        FLOAT_NUMBER,
        STRING,

        // indentation handling
        NEWLINE,
        INDENT,
        DEDENT,

        // operators
        LPAR,
        RPAR,
        LSQB,
        RSQB,
        COLON,
        COMMA,
        SEMI,
        PLUS,
        MINUS,
        STAR,
        SLASH,
        VBAR,
        AMPER,
        LESS,
        GREATER,
        EQUAL,
        DOT,
        PERCENT,
        LBRACE,
        RBRACE,
        EQEQUAL,
        NOTEQUAL,
        LESSEQUAL,
        GREATEREQUAL,
        TILDE,
        CIRCUMFLEX,
        LEFTSHIFT,
        RIGHTSHIFT,
        DOUBLESTAR,
        PLUSEQUAL,
        MINEQUAL,
        STAREQUAL,
        SLASHEQUAL,
        PERCENTEQUAL,
        AMPEREQUAL,
        VBAREQUAL,
        CIRCUMFLEXEQUAL,
        LEFTSHIFTEQUAL,
        RIGHTSHIFTEQUAL,
        DOUBLESTAREQUAL,
        DOUBLESLASH,
        DOUBLESLASHEQUAL,
        AT,
        ATEQUAL,
        RARROW,
        ELLIPSIS,
        COLONEQUAL,
        EXCLAMATION,

        // keywords -
        // https://docs.python.org/3/reference/lexical_analysis.html#keywords
        FALSE,
        NONE,
        TRUE,
        AND,
        AS,
        ASSERT,
        AWAIT,
        ASYNC,
        BREAK,
        CLASS,
        CONTINUE,
        DEF,
        DEL,
        ELIF,
        ELSE,
        EXCEPT,
        FINALLY,
        FOR,
        FROM,
        GLOBAL,
        IF,
        IMPORT,
        IN,
        IS,
        LAMBDA,
        NONLOCAL,
        NOT,
        OR,
        PASS,
        RAISE,
        RETURN,
        TRY,
        WHILE,
        WITH,
        YIELD,

        // OTHER
        TYPE_IGNORE,
        TYPE_COMMENT,
        ERRORTOKEN_INVALID_CHARACTER,
        ERRORTOKEN_UNTERMINATED_STRING,
        ERRORTOKEN_UNTERMINATED_TRIPLE_STRING,
        ERRORTOKEN_OPEN_BRACKET_EOF,
    };

    const wchar_t *to_wstring(Token t);
    std::string to_string(Token t);
    std::ostream &operator<<(std::ostream &o, Token t);

    class TokenVector
    {
    public:
        TokenVector(const CompilationUnit *_compilation_unit)
            : compilation_unit(_compilation_unit)
        {
        }

        const CompilationUnit *compilation_unit;
        std::vector<Token> tokens;
        std::vector<uint32_t> source_offsets;

        size_t size() const
        {
            assert(tokens.size() == source_offsets.size());
            return tokens.size();
        }

        void emplace_back(Token token, uint32_t source_offset)
        {
            tokens.push_back(token);
            source_offsets.push_back(source_offset);
        }
    };

}  // namespace cl

#endif  // CL_TOKEN_H
