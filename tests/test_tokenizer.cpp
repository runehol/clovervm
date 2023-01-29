#include <gtest/gtest.h>
#include "tokenizer.h"
#include "compilation_unit.h"
#include <vector>

using namespace cl;

TEST(CTensor, one_plus_two)
{
    CompilationUnit input(L"1 + 2");
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
