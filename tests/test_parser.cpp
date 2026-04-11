#include "ast_print.h"
#include "compilation_unit.h"
#include "parser.h"
#include "token_print.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <stdexcept>

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

static void expect_parse_error(const wchar_t *source,
                               const char *expected_message)
{
    try
    {
        (void)parse(source);
        FAIL() << "Expected std::runtime_error with message: "
               << expected_message;
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ(expected_message, err.what());
    }
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
                            "    return b\n"
                            "else:\n"
                            "    return a\n");
    std::string actual = parse(L""
                               "if a < b:\n"
                               "    return b\n"
                               "else:\n"
                               "    return a\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, def_stmt)
{
    std::string expected = (""
                            "def maybe_sub(n):\n"
                            "    if n <= 1:\n"
                            "        return n\n"
                            "    return n - 1\n");
    std::string actual = parse(L""
                               "def maybe_sub(n):\n"
                               "    if n <= 1:\n"
                               "        return n\n"
                               "    return n-1\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, rec_call)
{
    std::string expected = (""
                            "def fib(n):\n"
                            "    if n <= 1:\n"
                            "        return n\n"
                            "    return fib(n - 2) + fib(n - 1)\n");
    std::string actual = parse(L""
                               "def fib(n):\n"
                               "    if n <= 1:\n"
                               "        return n\n"
                               "    return fib(n-2) + fib(n-1)\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, missing_colon_in_if_stmt)
{
    expect_parse_error(L"if True\n"
                       L"    return 1\n",
                       "Expected token COLON, got NEWLINE");
}
