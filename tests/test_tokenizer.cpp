#include <gtest/gtest.h>
#include "tokenizer.h"
#include "compilation_unit.h"
#include "token_print.h"
#include <vector>

using namespace cl;

TEST(Tokenizer, simple)
{
    CompilationUnit input(L"amiga + windows");
    std::vector<Token> expected_tokens = {
        Token::NAME,
        Token::PLUS,
        Token::NAME,
        Token::NEWLINE,
        Token::ENDMARKER
    };

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}


TEST(Tokenizer, simple2)
{
    CompilationUnit input(L"12 + 345");
    std::vector<Token> expected_tokens = {
        Token::NUMBER,
        Token::PLUS,
        Token::NUMBER,
        Token::NEWLINE,
        Token::ENDMARKER
    };

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, factorial)
{
    std::wstring source =
       L"def recur_factorial(n):\n"
        "    if n == 1:\n"
        "        return n\n"
        "    else:\n"
        "        return n*recur_factorial(n-1)\n";

    CompilationUnit input(source);
    std::vector<Token> expected_tokens = {
        Token::DEF, Token::NAME, Token::LPAR, Token::NAME, Token::RPAR, Token::COLON, Token::NEWLINE,
        Token::INDENT, Token::IF, Token::NAME, Token::EQEQUAL, Token::NUMBER, Token::COLON, Token::NEWLINE,
        Token::INDENT, Token::RETURN, Token::NAME, Token::NEWLINE,
        Token::DEDENT, Token::ELSE, Token::COLON, Token::NEWLINE,
        Token::INDENT, Token::RETURN, Token::NAME, Token::STAR, Token::NAME, Token::LPAR, Token::NAME, Token::MINUS, Token::NUMBER, Token::RPAR, Token::NEWLINE,
        Token::DEDENT, Token::DEDENT, Token::ENDMARKER
    };

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}



TEST(Tokenizer, factorial_with_comments)
{
    std::wstring source =
       L" #zomg\n"
        "def recur_factorial(n):\n"
        "    if n == 1:#rofl\n"
        "        return n\n"
        "    else:\n"
        "        return n*recur_factorial(n-1)\n";

    CompilationUnit input(source);
    std::vector<Token> expected_tokens = {
        Token::NEWLINE,
        Token::DEF, Token::NAME, Token::LPAR, Token::NAME, Token::RPAR, Token::COLON, Token::NEWLINE,
        Token::INDENT, Token::IF, Token::NAME, Token::EQEQUAL, Token::NUMBER, Token::COLON, Token::NEWLINE,
        Token::INDENT, Token::RETURN, Token::NAME, Token::NEWLINE,
        Token::DEDENT, Token::ELSE, Token::COLON, Token::NEWLINE,
        Token::INDENT, Token::RETURN, Token::NAME, Token::STAR, Token::NAME, Token::LPAR, Token::NAME, Token::MINUS, Token::NUMBER, Token::RPAR, Token::NEWLINE,
        Token::DEDENT, Token::DEDENT, Token::ENDMARKER
    };

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}


TEST(Tokenizer, simple_strings)
{
    std::wstring source =
        L"\"abc\" \"def\"\n";
    CompilationUnit input(source);
    std::vector<Token> expected_tokens = {
        Token::STRING,
        Token::STRING,
        Token::NEWLINE,
        Token::ENDMARKER
    };

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}



TEST(Tokenizer, comment_issue)
{
    std::wstring source =
        L""
"# Program to display the Fibonacci sequence up to n-th term\n"
"\n"
"nterms = int(input(\"How many terms? \"))\n"
"\n"
"# first two terms\n"
"n1, n2 = 0, 1\n"
"count = 0\n"
"\n"
        ;
        ;
    CompilationUnit input(source);
    std::vector<Token> expected_tokens = {
        Token::NEWLINE,
        Token::NAME,
        Token::EQUAL,
        Token::NAME,
        Token::LPAR,
        Token::NAME,
        Token::LPAR,
        Token::STRING,
        Token::RPAR,
        Token::RPAR,
        Token::NEWLINE,
        Token::NAME,
        Token::COMMA,
        Token::NAME,
        Token::EQUAL,
        Token::NUMBER,
        Token::COMMA,
        Token::NUMBER,
        Token::NEWLINE,
        Token::NAME,
        Token::EQUAL,
        Token::NUMBER,
        Token::NEWLINE,
        Token::ENDMARKER
    };

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}
