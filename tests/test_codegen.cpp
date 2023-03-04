#include <gtest/gtest.h>
#include "code_object.h"
#include "thread_state.h"
#include "parser.h"
#include "virtual_machine.h"
#include "code_object_print.h"
#include <fmt/xchar.h>

using namespace cl;

std::string bytecode_str_from_expression(const wchar_t *expr)
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
        "00011 LdaSmi 4\n"
        "00013 AddSmi 3\n"
        "00007 MulSmi 2\n"
        "00002 AddSmi 1\n"
        "00000 Return\n";
    std::string actual = bytecode_str_from_expression(L"1 + 2  *  (4 + 3)");

    EXPECT_EQ(expected, actual);
}


TEST(Codegen, simple2)
{
    std::string expected =
        "Code object:\n"
        "00001 LdaSmi 1\n"
        "00003 LeftShiftSmi 4\n"
        "00009 AddSmi 3\n"
        "00000 Return\n";
    std::string actual = bytecode_str_from_expression(L"(1 << 4) + 3");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple3)
{
    std::string expected =
        "Code object:\n"
        "00004 LdaTrue\n"
        "00000 Not\n"
        "00000 Return\n";
    std::string actual = bytecode_str_from_expression(L"not True");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, simple4)
{
    std::string expected =
        "Code object:\n"
        "00000 LdaSmi 1\n"
        "00002 Star r0\n"
        "00011 LdaSmi 4\n"
        "00013 AddSmi 3\n"
        "00007 MulSmi 2\n"
        "00002 Sub r0\n"
        "00000 Return\n";
    std::string actual = bytecode_str_from_expression(L"1 - 2  *  (4 + 3)");

    EXPECT_EQ(expected, actual);
}
