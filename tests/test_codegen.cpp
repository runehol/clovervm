#include <gtest/gtest.h>
#include "code_object.h"
#include "thread_state.h"
#include "parser.h"
#include "virtual_machine.h"
#include "code_object_print.h"
#include <fmt/xchar.h>

using namespace cl;

std::string bytecode_str_from_file(const wchar_t *expr)
{
    VirtualMachine vm;
    CodeObject *code_obj = vm.get_default_thread()->compile(expr, StartRule::File);
    std::string actual = fmt::to_string(*code_obj);
    return actual;
}


TEST(Codegen, simple)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 4\n"
        "    2 AddSmi 3\n"
        "    4 MulSmi 2\n"
        "    6 AddSmi 1\n"
        "    8 Halt\n";
    std::string actual = bytecode_str_from_file(L"1 + 2  *  (4 + 3)");

    EXPECT_EQ(expected, actual);
}


TEST(Codegen, simple2)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 1\n"
        "    2 LeftShiftSmi 4\n"
        "    4 AddSmi 3\n"
        "    6 Halt\n";
    std::string actual = bytecode_str_from_file(L"(1 << 4) + 3");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple3)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaTrue\n"
        "    1 Not\n"
        "    2 Halt\n";
    std::string actual = bytecode_str_from_file(L"not True");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple4)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 1\n"
        "    2 Star0\n"
        "    3 LdaSmi 4\n"
        "    5 AddSmi 3\n"
        "    7 MulSmi 2\n"
        "    9 Sub r0\n"
        "   11 Halt\n";
    std::string actual = bytecode_str_from_file(L"1 - 2  *  (4 + 3)");

    EXPECT_EQ(expected, actual);
}


TEST(Codegen, assignment1)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 4\n"
        "    2 StaGlobal [0]\n"
        "    7 LdaGlobal [0]\n"
        "   12 AddSmi 3\n"
        "   14 Halt\n";
    std::string actual = bytecode_str_from_file(L"a = 4\n"
                                                "a + 3"
        );

    EXPECT_EQ(expected, actual);
}


TEST(Codegen, assignment2)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 4\n"
        "    2 StaGlobal [0]\n"
        "    7 LdaGlobal [0]\n"
        "   12 AddSmi 7\n"
        "   14 StaGlobal [0]\n"
        "   19 Halt\n";
    std::string actual = bytecode_str_from_file(L"a = 4\n"
                                                "a += 7\n"
        );

    EXPECT_EQ(expected, actual);
}


TEST(Codegen, while1)
{
    std::string expected =
        "Code object:\n"
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
                                                "while a: a -= 1\n"
        );

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, while2)
{
    const wchar_t *test_case =
        L"b = 0\n"
        "a = 100\n"
        "while a:\n"
        "    a -= 1\n"
        "    b += a\n"
        "b\n";

    std::string expected =
        "Code object:\n"
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
