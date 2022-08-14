#ifndef CL_TOKENIZER_H
#define CL_TOKENIZER_H

typedef enum token_kind
{
    TK_LPAREN,
    TK_RPAREN,
    TK_LBRACKET,
    TK_RBRACKET,
    TK_INTEGER,
    TK_STRING,
    TK_SYMBOL,
    TK_EOF
} token_kind;

typedef struct token
{
    token_kind kind;
    uint32_t source_offset;
    uint32_t size;
    uint32_t line;
    uint32_t column;
} token;


#endif //CL_TOKENIZER_H