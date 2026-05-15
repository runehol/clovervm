#include "compilation_unit.h"
#include "token_print.h"
#include "tokenizer.h"
#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

using namespace cl;

static void expect_number_spellings(const wchar_t *source,
                                    const std::vector<std::wstring> &expected)
{
    CompilationUnit input(source);
    TokenVector tv = tokenize(input);

    std::vector<std::wstring> actual;
    for(size_t i = 0; i < tv.tokens.size(); ++i)
    {
        if(tv.tokens[i] == Token::INT_NUMBER ||
           tv.tokens[i] == Token::FLOAT_NUMBER)
        {
            actual.emplace_back(
                string_for_number_token(input, tv.source_offsets[i]));
        }
    }

    EXPECT_EQ(expected, actual);
}

static void expect_tokenize_error(const wchar_t *source,
                                  const char *expected_message)
{
    try
    {
        CompilationUnit input(source);
        (void)tokenize(input);
        FAIL() << "Expected std::runtime_error with message: "
               << expected_message;
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ(expected_message, err.what());
    }
}

TEST(Tokenizer, simple)
{
    CompilationUnit input(L"amiga + windows");
    std::vector<Token> expected_tokens = {Token::NAME, Token::PLUS, Token::NAME,
                                          Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, simple2)
{
    CompilationUnit input(L"12 + 345");
    std::vector<Token> expected_tokens = {Token::INT_NUMBER, Token::PLUS,
                                          Token::INT_NUMBER, Token::NEWLINE,
                                          Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, unterminated_single_string_emits_error_token)
{
    CompilationUnit input(L"x = \"abc\n");
    std::vector<Token> expected_tokens = {Token::NAME, Token::EQUAL,
                                          Token::ERRORTOKEN_UNTERMINATED_STRING,
                                          Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, invalid_character_emits_error_token)
{
    CompilationUnit input(L"\u2764\ufe0f\n");
    std::vector<Token> expected_tokens = {Token::ERRORTOKEN_INVALID_CHARACTER,
                                          Token::ERRORTOKEN_INVALID_CHARACTER,
                                          Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, unterminated_triple_string_emits_error_token)
{
    CompilationUnit input(L"x = \"\"\"abc\n");
    std::vector<Token> expected_tokens = {
        Token::NAME, Token::EQUAL, Token::ERRORTOKEN_UNTERMINATED_TRIPLE_STRING,
        Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, open_bracket_at_eof_emits_error_token)
{
    CompilationUnit input(L"x = (1 +\n");
    std::vector<Token> expected_tokens = {
        Token::NAME,       Token::EQUAL, Token::LPAR,
        Token::INT_NUMBER, Token::PLUS,  Token::ERRORTOKEN_OPEN_BRACKET_EOF,
        Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, number_formats)
{
    CompilationUnit input(L"0xff + 0b1010 + 0o77 + 1_000_000");
    std::vector<Token> expected_tokens = {
        Token::INT_NUMBER, Token::PLUS,       Token::INT_NUMBER,
        Token::PLUS,       Token::INT_NUMBER, Token::PLUS,
        Token::INT_NUMBER, Token::NEWLINE,    Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
    expect_number_spellings(L"0xff + 0b1010 + 0o77 + 1_000_000",
                            {L"0xff", L"0b1010", L"0o77", L"1_000_000"});
}

TEST(Tokenizer, float_number_formats)
{
    CompilationUnit input(L"1.0 + 1. + .5 + 1e3 + 1E3 + 1.2e-3 + 1_2.3_4");
    std::vector<Token> expected_tokens = {
        Token::FLOAT_NUMBER, Token::PLUS,    Token::FLOAT_NUMBER, Token::PLUS,
        Token::FLOAT_NUMBER, Token::PLUS,    Token::FLOAT_NUMBER, Token::PLUS,
        Token::FLOAT_NUMBER, Token::PLUS,    Token::FLOAT_NUMBER, Token::PLUS,
        Token::FLOAT_NUMBER, Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
    expect_number_spellings(
        L"1.0 + 1. + .5 + 1e3 + 1E3 + 1.2e-3 + 1_2.3_4",
        {L"1.0", L"1.", L".5", L"1e3", L"1E3", L"1.2e-3", L"1_2.3_4"});
}

TEST(Tokenizer, invalid_float_exponent_does_not_extend_number_token)
{
    expect_number_spellings(L"1e+", {L"1"});
}

TEST(Tokenizer, factorial)
{
    std::wstring source = L"def recur_factorial(n):\n"
                          "    if n == 1:\n"
                          "        return n\n"
                          "    else:\n"
                          "        return n*recur_factorial(n-1)\n";

    CompilationUnit input(source);
    std::vector<Token> expected_tokens = {
        Token::DEF,     Token::NAME,    Token::LPAR,       Token::NAME,
        Token::RPAR,    Token::COLON,   Token::NEWLINE,    Token::INDENT,
        Token::IF,      Token::NAME,    Token::EQEQUAL,    Token::INT_NUMBER,
        Token::COLON,   Token::NEWLINE, Token::INDENT,     Token::RETURN,
        Token::NAME,    Token::NEWLINE, Token::DEDENT,     Token::ELSE,
        Token::COLON,   Token::NEWLINE, Token::INDENT,     Token::RETURN,
        Token::NAME,    Token::STAR,    Token::NAME,       Token::LPAR,
        Token::NAME,    Token::MINUS,   Token::INT_NUMBER, Token::RPAR,
        Token::NEWLINE, Token::DEDENT,  Token::DEDENT,     Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, factorial_with_comments)
{
    std::wstring source = L" #zomg\n"
                          "def recur_factorial(n):\n"
                          "    if n == 1:#rofl\n"
                          "        return n\n"
                          "    else:\n"
                          "        return n*recur_factorial(n-1)\n";

    CompilationUnit input(source);
    std::vector<Token> expected_tokens = {
        Token::NEWLINE,    Token::DEF,     Token::NAME,    Token::LPAR,
        Token::NAME,       Token::RPAR,    Token::COLON,   Token::NEWLINE,
        Token::INDENT,     Token::IF,      Token::NAME,    Token::EQEQUAL,
        Token::INT_NUMBER, Token::COLON,   Token::NEWLINE, Token::INDENT,
        Token::RETURN,     Token::NAME,    Token::NEWLINE, Token::DEDENT,
        Token::ELSE,       Token::COLON,   Token::NEWLINE, Token::INDENT,
        Token::RETURN,     Token::NAME,    Token::STAR,    Token::NAME,
        Token::LPAR,       Token::NAME,    Token::MINUS,   Token::INT_NUMBER,
        Token::RPAR,       Token::NEWLINE, Token::DEDENT,  Token::DEDENT,
        Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, simple_strings)
{
    std::wstring source = L"\"abc\" \"def\"\n";
    CompilationUnit input(source);
    std::vector<Token> expected_tokens = {Token::STRING, Token::STRING,
                                          Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, strings_support_prefixes_single_quotes_and_escapes)
{
    CompilationUnit input(L"'a\\'b' \"line\\n\" r\"raw\\\\n\" u'uni\\u263A'\n");
    std::vector<Token> expected_tokens = {Token::STRING,  Token::STRING,
                                          Token::STRING,  Token::STRING,
                                          Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);

    std::vector<std::wstring> expected_spellings = {
        L"'a\\'b'", L"\"line\\n\"", L"r\"raw\\\\n\"", L"u'uni\\u263A'"};
    std::vector<std::wstring> actual_spellings;
    for(size_t i = 0; i < tv.tokens.size(); ++i)
    {
        if(tv.tokens[i] == Token::STRING)
        {
            actual_spellings.emplace_back(
                string_for_string_token(input, tv.source_offsets[i]));
        }
    }
    EXPECT_EQ(expected_spellings, actual_spellings);
}

TEST(Tokenizer, triple_quoted_strings_may_span_lines)
{
    CompilationUnit input(L"\"\"\"first\n"
                          L"second\"\"\"\n"
                          L"r'''raw\n"
                          L"text'''\n");
    std::vector<Token> expected_tokens = {Token::STRING, Token::NEWLINE,
                                          Token::STRING, Token::NEWLINE,
                                          Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);

    std::vector<std::wstring> expected_spellings = {
        L"\"\"\"first\nsecond\"\"\"", L"r'''raw\ntext'''"};
    std::vector<std::wstring> actual_spellings;
    for(size_t i = 0; i < tv.tokens.size(); ++i)
    {
        if(tv.tokens[i] == Token::STRING)
        {
            actual_spellings.emplace_back(
                string_for_string_token(input, tv.source_offsets[i]));
        }
    }
    EXPECT_EQ(expected_spellings, actual_spellings);
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
        "\n";
    ;
    CompilationUnit input(source);
    std::vector<Token> expected_tokens = {
        Token::NEWLINE, Token::NAME,       Token::EQUAL,   Token::NAME,
        Token::LPAR,    Token::NAME,       Token::LPAR,    Token::STRING,
        Token::RPAR,    Token::RPAR,       Token::NEWLINE, Token::NAME,
        Token::COMMA,   Token::NAME,       Token::EQUAL,   Token::INT_NUMBER,
        Token::COMMA,   Token::INT_NUMBER, Token::NEWLINE, Token::NAME,
        Token::EQUAL,   Token::INT_NUMBER, Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, leading_comment_and_blank_lines_collapse_to_one_newline)
{
    CompilationUnit input(L"# first comment\n"
                          L"\n"
                          L"   # second comment\n"
                          L"value = 1\n");
    std::vector<Token> expected_tokens = {Token::NEWLINE, Token::NAME,
                                          Token::EQUAL,   Token::INT_NUMBER,
                                          Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, interior_comment_and_blank_lines_collapse_to_one_newline)
{
    CompilationUnit input(L"a = 1\n"
                          L"# comment between statements\n"
                          L"\n"
                          L"  # indented comment between statements\n"
                          L"b = 2\n");
    std::vector<Token> expected_tokens = {
        Token::NAME,       Token::EQUAL,   Token::INT_NUMBER,
        Token::NEWLINE,    Token::NAME,    Token::EQUAL,
        Token::INT_NUMBER, Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, blank_line_inside_block_emits_newline_without_indent_changes)
{
    CompilationUnit input(L"if True:\n"
                          L"    a = 1\n"
                          L"\n"
                          L"    # comment-only line in block\n"
                          L"    b = 2\n");
    std::vector<Token> expected_tokens = {
        Token::IF,      Token::TRUE,   Token::COLON,    Token::NEWLINE,
        Token::INDENT,  Token::NAME,   Token::EQUAL,    Token::INT_NUMBER,
        Token::NEWLINE, Token::NAME,   Token::EQUAL,    Token::INT_NUMBER,
        Token::NEWLINE, Token::DEDENT, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, newlines_inside_brackets_do_not_emit_newline_or_indent_tokens)
{
    CompilationUnit input(L"value = f(\n"
                          L"    [1,\n"
                          L"     2],\n"
                          L"    {3: 4}\n"
                          L")\n");
    std::vector<Token> expected_tokens = {
        Token::NAME,    Token::EQUAL,      Token::NAME,   Token::LPAR,
        Token::LSQB,    Token::INT_NUMBER, Token::COMMA,  Token::INT_NUMBER,
        Token::RSQB,    Token::COMMA,      Token::LBRACE, Token::INT_NUMBER,
        Token::COLON,   Token::INT_NUMBER, Token::RBRACE, Token::RPAR,
        Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, comments_inside_brackets_do_not_end_statement)
{
    CompilationUnit input(L"value = (\n"
                          L"    1,  # first\n"
                          L"    2\n"
                          L")\n");
    std::vector<Token> expected_tokens = {
        Token::NAME,       Token::EQUAL,   Token::LPAR,
        Token::INT_NUMBER, Token::COMMA,   Token::INT_NUMBER,
        Token::RPAR,       Token::NEWLINE, Token::ENDMARKER};

    TokenVector tv = tokenize(input);
    EXPECT_EQ(tv.tokens, expected_tokens);
}

TEST(Tokenizer, bad_indentation)
{
    expect_tokenize_error(L"if True:\n"
                          L"    a = 1\n"
                          L"  b = 2\n",
                          "IndentationError: unindent does not match any outer "
                          "indentation level 2");
}
