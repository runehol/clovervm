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


static Value run_file(const wchar_t *str)
{
    VirtualMachine vm;
    CodeObject code_obj = vm.get_default_thread()->compile(str, StartRule::File);
    return vm.get_default_thread()->run(&code_obj);
}

TEST(Interpreter, simple)
{
    Value expected = Value::from_smi(15);
    Value actual = run_file(L"1 + 2  *  (4 + 3)");
    EXPECT_EQ(expected, actual);
}


TEST(Interpreter, simple2)
{
    Value expected = Value::from_smi(19);
    Value actual = run_file(L"(1 << 4) + 3");
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple3)
{
    Value expected = Value::False();
    Value actual = run_file(L"not True");
    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, simple4)
{
    Value expected = Value::from_smi(-13);
    Value actual = run_file(L"1 - 2  *  (4 + 3)");
    EXPECT_EQ(expected, actual);
}


TEST(Interpreter, assignment1)
{
    Value expected = Value::from_smi(7);
    Value actual = run_file(L"a = 4\n"
                            "a + 3"
        );

    EXPECT_EQ(expected, actual);
}


TEST(Interpreter, assignment2)
{
    Value expected = Value::from_smi(11);
    Value actual = run_file(L"a = 4\n"
                            "a += 7\n"
        );

    EXPECT_EQ(expected, actual);
}


TEST(Interpreter, while1)
{
    Value expected = Value::from_smi(4950);
    Value actual = run_file(L"b = 0\n"
                            "a = 100\n"
                            "while a:\n"
                            "    a -= 1\n"
                            "    b += a\n"
                            "b\n"
        );

    EXPECT_EQ(expected, actual);
}

/*
TEST(Interpreter, small_bench)
{
    const wchar_t *test_case =
        L"counter = 100000001\n"
        "acc = 1245\n"
        "while counter:\n"
        "    acc = (-acc*64 + 64)>>6\n"
        "    counter -= 1\n"
        "acc\n";

    Value expected = Value::from_smi(-1244);
    Value actual = run_file(test_case);

    EXPECT_EQ(expected, actual);
}
*/
