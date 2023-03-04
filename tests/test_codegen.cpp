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
    CodeObject code_obj = vm.get_default_thread()->compile(expr, StartRule::File);
    std::string actual = fmt::to_string(code_obj);
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
        "    8 Return\n";
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
        "    6 Return\n";
    std::string actual = bytecode_str_from_file(L"(1 << 4) + 3");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple3)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaTrue\n"
        "    1 Not\n"
        "    2 Return\n";
    std::string actual = bytecode_str_from_file(L"not True");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple4)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 1\n"
        "    2 Star r0\n"
        "    4 LdaSmi 4\n"
        "    6 AddSmi 3\n"
        "    8 MulSmi 2\n"
        "   10 Sub r0\n"
        "   12 Return\n";
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
        "   14 Return\n";
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
        "   19 Return\n";
    std::string actual = bytecode_str_from_file(L"a = 4\n"
                                                "a += 7\n"
        );

    EXPECT_EQ(expected, actual);
}
