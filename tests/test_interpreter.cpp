#include <gtest/gtest.h>
#include "tokenizer.h"
#include "compilation_unit.h"
#include "token_print.h"
#include "parser.h"
#include "codegen.h"
#include "interpreter.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>

using namespace cl;


static Value run_expression(const wchar_t *str)
{
    VirtualMachine vm;
    CodeObject code_obj = vm.get_default_thread()->compile(str, StartRule::Eval);
    return vm.get_default_thread()->run(&code_obj);
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
    Value expected = Value::False();
    Value actual = run_expression(L"not True");
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple4)
{
    Value expected = Value::from_smi(-13);
    Value actual = run_expression(L"1 - 2  *  (4 + 3)");
    EXPECT_EQ(expected, actual);
}
