#include "codegen.h"
#include "compilation_unit.h"
#include "interpreter.h"
#include "parser.h"
#include "thread_state.h"
#include "token_print.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <stdexcept>

using namespace cl;

static constexpr int64_t kMinSmi = -288230376151711744LL;

static Value run_file(const wchar_t *str)
{
    VirtualMachine vm;
    CodeObject *code_obj =
        vm.get_default_thread()->compile(str, StartRule::File);
    return vm.get_default_thread()->run(code_obj);
}

static void expect_runtime_error(const wchar_t *source,
                                 const char *expected_message)
{
    try
    {
        (void)run_file(source);
        FAIL() << "Expected std::runtime_error with message: "
               << expected_message;
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ(expected_message, err.what());
    }
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
                            "a + 3");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, assignment2)
{
    Value expected = Value::from_smi(11);
    Value actual = run_file(L"a = 4\n"
                            "a += 7\n");

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
                            "b\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, if_elif_branch)
{
    Value expected = Value::from_smi(2);
    Value actual = run_file(L"a = False\n"
                            "b = True\n"
                            "if a:\n"
                            "    1\n"
                            "elif b:\n"
                            "    2\n"
                            "else:\n"
                            "    3\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, if_else_branch)
{
    Value expected = Value::from_smi(3);
    Value actual = run_file(L"a = False\n"
                            "b = False\n"
                            "if a:\n"
                            "    1\n"
                            "elif b:\n"
                            "    2\n"
                            "else:\n"
                            "    3\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, while_else_runs_after_normal_exit)
{
    Value expected = Value::from_smi(7);
    Value actual = run_file(L"a = 2\n"
                            "b = 0\n"
                            "while a:\n"
                            "    a -= 1\n"
                            "else:\n"
                            "    b = 7\n"
                            "b\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, while_else_skipped_after_break)
{
    Value expected = Value::from_smi(0);
    Value actual = run_file(L"a = 2\n"
                            "b = 0\n"
                            "while a:\n"
                            "    break\n"
                            "else:\n"
                            "    b = 7\n"
                            "b\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, recursive_fibonacci)
{
    Value expected = Value::from_smi(10946);
    Value actual = run_file(L"def fib(n):\n"
                            "    if n <= 2:\n"
                            "        return n\n"
                            "    return fib(n-2) + fib(n-1)\n"
                            "\n"
                            "fib(20)\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, name_error)
{
    expect_runtime_error(L"missing_name\n", "NameError");
}

TEST(Interpreter, call_non_callable)
{
    expect_runtime_error(L"1()\n", "TypeError: object is not callable");
}

TEST(Interpreter, negate_expression)
{
    Value expected = Value::from_smi(-7);
    Value actual = run_file(L"-(3 + 4)\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, left_shift_negative_count)
{
    expect_runtime_error(L"a = 1\n"
                         L"b = -1\n"
                         L"a << b\n",
                         "ValueError: negative shift count");
}

TEST(Interpreter, right_shift_negative_count)
{
    expect_runtime_error(L"a = 8\n"
                         L"b = -1\n"
                         L"a >> b\n",
                         "ValueError: negative shift count");
}

TEST(Interpreter, left_shift_boundary)
{
    Value expected = Value::from_smi(kMinSmi);
    Value actual = run_file(L"(-1) << 58\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, left_shift_overflow_smi)
{
    expect_runtime_error(L"1 << 58\n", "Clovervm exception");
}

TEST(Interpreter, left_shift_overflow_register)
{
    expect_runtime_error(L"a = 1\n"
                         L"b = 58\n"
                         L"a << b\n",
                         "Clovervm exception");
}

TEST(Interpreter, right_shift_negative_value)
{
    Value expected = Value::from_smi(-5);
    Value actual = run_file(L"(-9) >> 1\n");

    EXPECT_EQ(expected, actual);
}

TEST(Interpreter, add_overflow)
{
    expect_runtime_error(L"288230376151711743 + 1\n", "Clovervm exception");
}

TEST(Interpreter, subtract_overflow)
{
    expect_runtime_error(L"-288230376151711743 - 2\n", "Clovervm exception");
}

TEST(Interpreter, multiply_overflow)
{
    expect_runtime_error(L"288230376151711743 * 2\n", "Clovervm exception");
}

TEST(Interpreter, negate_overflow)
{
    Value expected = Value::from_smi(kMinSmi);
    Value actual = run_file(L"-288230376151711743 - 1\n");
    EXPECT_EQ(expected, actual);

    expect_runtime_error(L"x = -288230376151711743 - 1\n"
                         L"-x\n",
                         "Clovervm exception");
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
