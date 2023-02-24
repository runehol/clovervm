#include <gtest/gtest.h>
#include "tokenizer.h"
#include "compilation_unit.h"
#include "token_print.h"
#include "parser.h"
#include "codegen.h"
#include "interpreter.h"
#include <fmt/xchar.h>

using namespace cl;


static Value run_expression(const wchar_t *str)
{
    CompilationUnit input(str);
    TokenVector tv = tokenize(input);
    AstVector av = parse(tv, StartRule::Eval);
    CodeObject code_obj = generate_code(av);
    return run_interpreter(&code_obj, 0);
}

TEST(Interpreter, simple)
{
    Value expected = Value::from_smi(15);
    Value actual = run_expression(L"1 + 2  *  (4 + 3)");
    EXPECT_EQ(expected, actual);
}


TEST(Interpreter, simple2)
{
    Value expected = Value::from_smi(19);
    Value actual = run_expression(L"(1 << 4) + 3");
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple3)
{
    Value expected = cl_False;
    Value actual = run_expression(L"not True");
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple4)
{
    Value expected = Value::from_smi(-13);
    Value actual = run_expression(L"1 - 2  *  (4 + 3)");
    EXPECT_EQ(expected, actual);
}
