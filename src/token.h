#ifndef CL_TOKEN_H
#define CL_TOKEN_H

#include <vector>
#include <cstdint>
#include <cassert>

namespace cl
{

    enum class Token : uint8_t
    {
        //end of file
        ENDMARKER,

        //metaclasses
        NAME,
        NUMBER,
        STRING,

        //indentation handling
        NEWLINE,
        INDENT,
        DEDENT,

        //operators
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

        //keywords - https://docs.python.org/3/reference/lexical_analysis.html#keywords
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

        //OTHER
        TYPE_IGNORE,
        TYPE_COMMENT,
        ERRORTOKEN,
    };


    struct TokenVector
    {
        std::vector<Token> tokens;
        std::vector<uint32_t> source_offsets;

        size_t size() const {
            assert(tokens.size() == source_offsets.size());
            return tokens.size();
        }

        void emplace_back(Token token, uint32_t source_offset)
        {
            tokens.push_back(token);
            source_offsets.push_back(source_offset);
        }
    };

}

#endif //CL_TOKEN_H
