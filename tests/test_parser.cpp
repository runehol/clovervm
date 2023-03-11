#include <gtest/gtest.h>
#include "virtual_machine.h"
#include "tokenizer.h"
#include "compilation_unit.h"
#include "token_print.h"
#include "parser.h"
#include "ast_print.h"
#include <fmt/xchar.h>

using namespace cl;

static std::string parse(const wchar_t *in_str)
{
    VirtualMachine vm;
    CompilationUnit input(in_str);
    TokenVector tv = tokenize(input);
    AstVector av = parse(vm, tv, StartRule::File);
    std::string actual = fmt::to_string(av);
    return actual;
}


TEST(Parser, simple)
{
    std::string expected = "1 + 2 * (4 + 3)\n";
    std::string actual = parse(L"1 + 2  *  (4 + 3)");

    EXPECT_EQ(expected, actual);
}


TEST(Parser, simple2)
{
    std::string expected = "(1 << 4) + 3\n";
    std::string actual = parse(L"(1 << 4) + 3");
    EXPECT_EQ(expected, actual);
}


TEST(Parser, if_stmt)
{
    std::string expected = ("if a < b:\n"
                               "        return b\n"
                               "else:\n"
                               "        return a\n");
    std::string actual = parse(L""
                               "if a < b:\n"
                               "    return b\n"
                               "else:\n"
                               "    return a\n");

    EXPECT_EQ(expected, actual);
}
