#include <gtest/gtest.h>
#include "tokenizer.h"
#include "compilation_unit.h"
#include "token_print.h"
#include "parser.h"
#include "codegen.h"
#include "interpreter.h"
#include <fmt/xchar.h>

using namespace cl;


static CLValue run_expression(const wchar_t *str)
{
    CompilationUnit input(str);
    TokenVector tv = tokenize(input);
    AstVector av = parse(tv, StartRule::Eval);
    CodeObject code_obj = generate_code(av);
    return run_interpreter(&code_obj, 0);
}

TEST(Interpreter, simple)
{
    CLValue expected = value_make_smi(15);
    CLValue actual = run_expression(L"1 + 2  *  (4 + 3)");
    EXPECT_EQ(expected, actual);
}


TEST(Interpreter, simple2)
{
    CLValue expected = value_make_smi(19);
    CLValue actual = run_expression(L"(1 << 4) + 3");
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple3)
{
    CLValue expected = cl_False;
    CLValue actual = run_expression(L"not True");
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple4)
{
    CLValue expected = value_make_smi(-13);
    CLValue actual = run_expression(L"1 - 2  *  (4 + 3)");
    EXPECT_EQ(expected, actual);
}
