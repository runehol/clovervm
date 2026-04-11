#include "code_object.h"
#include "code_object_print.h"
#include "parser.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>

using namespace cl;

std::string bytecode_str_from_file(const wchar_t *expr)
{
    VirtualMachine vm;
    CodeObject *code_obj =
        vm.get_default_thread()->compile(expr, StartRule::File);
    std::string actual = fmt::to_string(*code_obj);
    return actual;
}

TEST(Codegen, simple)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 Star0\n"
                           "    3 LdaSmi 2\n"
                           "    5 Star1\n"
                           "    6 LdaSmi 4\n"
                           "    8 AddSmi 3\n"
                           "   10 Mul r1\n"
                           "   12 Add r0\n"
                           "   14 Halt\n";
    std::string actual = bytecode_str_from_file(L"1 + 2  *  (4 + 3)");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple2)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 LeftShiftSmi 4\n"
                           "    4 AddSmi 3\n"
                           "    6 Halt\n";
    std::string actual = bytecode_str_from_file(L"(1 << 4) + 3");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple3)
{
    std::string expected = "Code object:\n"
                           "    0 LdaTrue\n"
                           "    1 Not\n"
                           "    2 Halt\n";
    std::string actual = bytecode_str_from_file(L"not True");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple4)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 Star0\n"
                           "    3 LdaSmi 2\n"
                           "    5 Star1\n"
                           "    6 LdaSmi 4\n"
                           "    8 AddSmi 3\n"
                           "   10 Mul r1\n"
                           "   12 Sub r0\n"
                           "   14 Halt\n";
    std::string actual = bytecode_str_from_file(L"1 - 2  *  (4 + 3)");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, assignment1)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 4\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaGlobal [0]\n"
                           "   12 AddSmi 3\n"
                           "   14 Halt\n";
    std::string actual = bytecode_str_from_file(L"a = 4\n"
                                                "a + 3");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, assignment2)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 4\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaGlobal [0]\n"
                           "   12 AddSmi 7\n"
                           "   14 StaGlobal [0]\n"
                           "   19 Halt\n";
    std::string actual = bytecode_str_from_file(L"a = 4\n"
                                                "a += 7\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, while1)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 4\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaGlobal [0]\n"
                           "   12 JumpIfFalse 35\n"
                           "   15 LdaGlobal [0]\n"
                           "   20 SubSmi 1\n"
                           "   22 StaGlobal [0]\n"
                           "   27 LdaGlobal [0]\n"
                           "   32 JumpIfTrue 15\n"
                           "   35 Halt\n";
    std::string actual = bytecode_str_from_file(L"a = 4\n"
                                                "while a: a -= 1\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, while2)
{
    const wchar_t *test_case = L"b = 0\n"
                               "a = 100\n"
                               "while a:\n"
                               "    a -= 1\n"
                               "    b += a\n"
                               "b\n";

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 0\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaSmi 100\n"
                           "    9 StaGlobal [1]\n"
                           "   14 LdaGlobal [1]\n"
                           "   19 JumpIfFalse 60\n"
                           "   22 LdaGlobal [1]\n"
                           "   27 SubSmi 1\n"
                           "   29 StaGlobal [1]\n"
                           "   34 LdaGlobal [0]\n"
                           "   39 Star0\n"
                           "   40 LdaGlobal [1]\n"
                           "   45 Add r0\n"
                           "   47 StaGlobal [0]\n"
                           "   52 LdaGlobal [1]\n"
                           "   57 JumpIfTrue 22\n"
                           "   60 LdaGlobal [0]\n"
                           "   65 Halt\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, if_elif_else)
{
    const wchar_t *test_case = L"if a:\n"
                               "    b = 1\n"
                               "elif c:\n"
                               "    b = 2\n"
                               "else:\n"
                               "    b = 3\n"
                               "b\n";

    std::string expected = "Code object:\n"
                           "    0 LdaGlobal [0]\n"
                           "    5 JumpIfFalse 18\n"
                           "    8 LdaSmi 1\n"
                           "   10 StaGlobal [1]\n"
                           "   15 Jump 43\n"
                           "   18 LdaGlobal [2]\n"
                           "   23 JumpIfFalse 36\n"
                           "   26 LdaSmi 2\n"
                           "   28 StaGlobal [1]\n"
                           "   33 Jump 43\n"
                           "   36 LdaSmi 3\n"
                           "   38 StaGlobal [1]\n"
                           "   43 LdaGlobal [1]\n"
                           "   48 Halt\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, while_else)
{
    const wchar_t *test_case = L"a = 2\n"
                               "b = 0\n"
                               "while a:\n"
                               "    a -= 1\n"
                               "else:\n"
                               "    b = 7\n"
                               "b\n";

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 2\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaSmi 0\n"
                           "    9 StaGlobal [1]\n"
                           "   14 LdaGlobal [0]\n"
                           "   19 JumpIfFalse 42\n"
                           "   22 LdaGlobal [0]\n"
                           "   27 SubSmi 1\n"
                           "   29 StaGlobal [0]\n"
                           "   34 LdaGlobal [0]\n"
                           "   39 JumpIfTrue 22\n"
                           "   42 LdaSmi 7\n"
                           "   44 StaGlobal [1]\n"
                           "   49 LdaGlobal [1]\n"
                           "   54 Halt\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}
