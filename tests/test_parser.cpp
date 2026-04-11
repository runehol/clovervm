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
                       "Expected token COLON, got NEWLINE at offset 7 (line 1, "
                       "column 8), near \"if True\"");
}

TEST(Parser, import_stmt_not_implemented)
{
    expect_parse_error(L"import math\n",
                       "Not implemented: import statement (token IMPORT) at "
                       "offset 0 (line 1, column 1), near \"import math\"");
}

TEST(Parser, class_def_not_implemented)
{
    expect_parse_error(L"class C:\n"
                       L"    pass\n",
                       "Not implemented: class definition (token CLASS) at "
                       "offset 0 (line 1, column 1), near \"class C:\"");
}

TEST(Parser, for_stmt_not_implemented)
{
    expect_parse_error(L"for x in y:\n"
                       L"    break\n",
                       "Not implemented: for statement (token FOR) at offset 0 "
                       "(line 1, column 1), near \"for x in y:\"");
}

TEST(Parser, yield_stmt_not_implemented)
{
    expect_parse_error(L"yield 1\n",
                       "Not implemented: yield statement (token YIELD) at "
                       "offset 0 (line 1, column 1), near \"yield 1\"");
}

TEST(Parser, tuple_assignment_target_not_supported)
{
    expect_parse_error(L"a, b = 1, 2\n",
                       "SyntaxError: assignment target must be a simple "
                       "variable at offset 0 (line 1, column 1), near "
                       "\"a, b = 1, 2\"");
}

TEST(Parser, expression_assignment_target_not_supported)
{
    expect_parse_error(L"a + b = 1\n",
                       "SyntaxError: assignment target must be a simple "
                       "variable at offset 0 (line 1, column 1), near "
                       "\"a + b = 1\"");
}

TEST(Parser, unexpected_token_reports_location)
{
    expect_parse_error(L")\n",
                       "Unexpected token RPAR at offset 0 (line 1, column 1), "
                       "near \")\"");
}

TEST(Parser, parse_error_reports_tab_expanded_column)
{
    expect_parse_error(L"if\tTrue\n"
                       L"    return 1\n",
                       "Expected token COLON, got NEWLINE at offset 7 (line 1, "
                       "column 13), near \"if\tTrue\"");
}

TEST(Parser, parse_error_reports_crlf_line_numbers)
{
    expect_parse_error(L"if True:\r\n"
                       L"    return (\r\n",
                       "Unexpected token NEWLINE at offset 22 (line 2, column "
                       "13), near \"    return (\"");
}
