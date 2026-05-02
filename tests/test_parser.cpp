#include "ast_print.h"
#include "compilation_unit.h"
#include "parser.h"
#include "str.h"
#include "test_helpers.h"
#include "token_print.h"
#include "tokenizer.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <stdexcept>

using namespace cl;

static std::string parse(const wchar_t *in_str)
{
    test::ParsedFile parsed(in_str);
    std::string actual = fmt::to_string(parsed.ast);
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

TEST(Parser, if_elif_else_stmt)
{
    std::string expected = ("if a:\n"
                            "    return 1\n"
                            "elif b:\n"
                            "    return 2\n"
                            "else:\n"
                            "    return 3\n");
    std::string actual = parse(L""
                               "if a:\n"
                               "    return 1\n"
                               "elif b:\n"
                               "    return 2\n"
                               "else:\n"
                               "    return 3\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, while_else_stmt)
{
    std::string expected = ("while a:\n"
                            "    a -= 1\n"
                            "else:\n"
                            "    return a\n");
    std::string actual = parse(L""
                               "while a:\n"
                               "    a -= 1\n"
                               "else:\n"
                               "    return a\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, for_stmt)
{
    std::string expected = ("for x in y:\n"
                            "    x\n");
    std::string actual = parse(L""
                               "for x in y:\n"
                               "    x\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, for_else_stmt)
{
    std::string expected = ("for x in range(3):\n"
                            "    x\n"
                            "else:\n"
                            "    return 1\n");
    std::string actual = parse(L""
                               "for x in range(3):\n"
                               "    x\n"
                               "else:\n"
                               "    return 1\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, global_stmt)
{
    std::string expected = "global a, b\n";
    std::string actual = parse(L"global a, b\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, string_literal_stores_constant_value)
{
    test::ParsedFile parsed(L"\"abc\"\n");

    EXPECT_TRUE(parsed.ast.kinds[parsed.ast.root_node].node_kind ==
                AstNodeKind::STATEMENT_SEQUENCE);

    int32_t stmt_idx = parsed.ast.children[parsed.ast.root_node][0];
    EXPECT_TRUE(parsed.ast.kinds[stmt_idx].node_kind ==
                AstNodeKind::STATEMENT_EXPRESSION);

    int32_t literal_idx = parsed.ast.children[stmt_idx][0];
    EXPECT_TRUE(parsed.ast.kinds[literal_idx].node_kind ==
                AstNodeKind::EXPRESSION_LITERAL);
    EXPECT_TRUE(parsed.ast.kinds[literal_idx].operator_kind ==
                AstOperatorKind::STRING);
    EXPECT_STREQ(L"abc", string_as_wchar_t(TValue<String>(
                             parsed.ast.constants[literal_idx])));
}

TEST(Parser, string_literal_decodes_escapes_and_prefixes)
{
    test::ParsedFile escaped(L"\"line\\n\\x41\\101\"\n");
    int32_t escaped_stmt = escaped.ast.children[escaped.ast.root_node][0];
    int32_t escaped_literal = escaped.ast.children[escaped_stmt][0];
    EXPECT_STREQ(L"line\nAA", string_as_wchar_t(TValue<String>(
                                  escaped.ast.constants[escaped_literal])));

    test::ParsedFile raw(L"r\"line\\n\"\n"
                         L"u'\\u263A'\n");
    int32_t raw_stmt = raw.ast.children[raw.ast.root_node][0];
    int32_t raw_literal = raw.ast.children[raw_stmt][0];
    EXPECT_STREQ(L"line\\n", string_as_wchar_t(TValue<String>(
                                 raw.ast.constants[raw_literal])));

    int32_t unicode_stmt = raw.ast.children[raw.ast.root_node][1];
    int32_t unicode_literal = raw.ast.children[unicode_stmt][0];
    EXPECT_STREQ(L"☺", string_as_wchar_t(
                           TValue<String>(raw.ast.constants[unicode_literal])));
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

TEST(Parser, calls_accept_trailing_comma)
{
    std::string expected = (""
                            "f(1)\n"
                            "f(1, 2)\n");
    std::string actual = parse(L"f(1,)\n"
                               L"f(1, 2,)\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, parameters_accept_trailing_comma)
{
    std::string expected = (""
                            "def f(a):\n"
                            "    return a\n"
                            "def g(a, b):\n"
                            "    return a + b\n");
    std::string actual = parse(L"def f(a,):\n"
                               L"    return a\n"
                               L"def g(a, b,):\n"
                               L"    return a + b\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, parameters_accept_defaults)
{
    std::string expected = (""
                            "def f(a=1, b=2):\n"
                            "    return a + b\n");
    std::string actual = parse(L"def f(a=1, b=2):\n"
                               L"    return a + b\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, parameters_accept_varargs)
{
    std::string expected = (""
                            "def f(a, b=1, *args):\n"
                            "    return args\n");
    std::string actual = parse(L"def f(a, b=1, *args):\n"
                               L"    return args\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, keyword_only_parameters_are_not_implemented)
{
    expect_parse_error(
        L"def f(*args, keyword_only):\n"
        L"    return args\n",
        "SyntaxError: keyword-only parameters are not implemented yet");
}

TEST(Parser, multiple_varargs_parameters_are_not_implemented)
{
    expect_parse_error(L"def f(*args, *rest):\n"
                       L"    return args\n",
                       "SyntaxError: * argument may appear only once");
}

TEST(Parser, function_and_method_parameter_annotations_parse)
{
    std::string expected = (""
                            "def f(a, b):\n"
                            "    return a + b\n"
                            "class C:\n"
                            "    def method(self, x):\n"
                            "        return x\n");
    std::string actual = parse(L"def f(a: int, b: list[int]) -> str:\n"
                               L"    return a + b\n"
                               L"class C:\n"
                               L"    def method(self, x: list[int]):\n"
                               L"        return x\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, variable_annotations_parse_and_are_ignored)
{
    std::string expected = (""
                            "x: int\n"
                            "y: list[int] = 1\n"
                            "class C:\n"
                            "    value: list[int]\n"
                            "    other: float = 2\n"
                            "def g():\n"
                            "    local: tuple[int]\n"
                            "    typed: int = 3\n"
                            "    return typed\n");
    std::string actual = parse(L"x: int\n"
                               L"y: list[int] = 1\n"
                               L"class C:\n"
                               L"    value: list[int]\n"
                               L"    other: float = 2\n"
                               L"def g():\n"
                               L"    local: tuple[int]\n"
                               L"    typed: int = 3\n"
                               L"    return typed\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, attribute_expression_and_assignment)
{
    std::string expected = ("obj.value\n"
                            "obj.value = 1\n"
                            "del obj.value\n"
                            "del value\n"
                            "del items[0]\n");
    std::string actual = parse(L"obj.value\n"
                               L"obj.value = 1\n"
                               L"del obj.value\n"
                               L"del value\n"
                               L"del items[0]\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, list_literals)
{
    std::string expected = ("[]\n"
                            "[1, 2, 3]\n"
                            "[1, 2]\n");
    std::string actual = parse(L"[]\n"
                               L"[1, 2, 3]\n"
                               L"[1, 2,]\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, tuple_literals)
{
    std::string expected = ("(())\n"
                            "((1,))\n"
                            "((1, 2, 3))\n"
                            "((1, 2))\n"
                            "(1 + 2) * 3\n");
    std::string actual = parse(L"()\n"
                               L"(1,)\n"
                               L"(1, 2, 3)\n"
                               L"(1, 2,)\n"
                               L"(1 + 2) * 3\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, dict_literals)
{
    std::string expected = ("{}\n"
                            "{1: 2, 3: 4}\n"
                            "{1: 2}\n");
    std::string actual = parse(L"{}\n"
                               L"{1: 2, 3: 4}\n"
                               L"{1: 2,}\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, subscript_expression_and_assignment)
{
    std::string expected = ("obj[1]\n"
                            "obj[idx] = value\n"
                            "obj[idx] += 1\n");
    std::string actual = parse(L"obj[1]\n"
                               L"obj[idx] = value\n"
                               L"obj[idx] += 1\n");

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

TEST(Parser, class_def)
{
    std::string expected = ("class C:\n"
                            "    pass\n");
    std::string actual = parse(L"class C:\n"
                               L"    pass\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, class_def_with_bases_and_members)
{
    std::string expected = ("class C(Base1, Base2):\n"
                            "    value = 1\n"
                            "    def method(self):\n"
                            "        return self.value\n");
    std::string actual = parse(L"class C(Base1, Base2):\n"
                               L"    value = 1\n"
                               L"    def method(self):\n"
                               L"        return self.value\n");

    EXPECT_EQ(expected, actual);
}

TEST(Parser, for_stmt_target_not_supported)
{
    expect_parse_error(L"for a, b in pairs:\n"
                       L"    a\n",
                       "SyntaxError: assignment target must be a simple "
                       "variable, attribute, or subscript at offset 4 (line 1, "
                       "column 5), near "
                       "\"for a, b in pairs:\"");
}

TEST(Parser, yield_stmt_not_implemented)
{
    expect_parse_error(L"yield 1\n",
                       "Not implemented: yield statement (token YIELD) at "
                       "offset 0 (line 1, column 1), near \"yield 1\"");
}

TEST(Parser, del_invalid_target_mentions_subscript)
{
    expect_parse_error(
        L"del items + 1\n",
        "SyntaxError: del target must be a variable, attribute, or subscript "
        "at offset 4 (line 1, column 5), near \"del items + 1\"");
}

TEST(Parser, tuple_assignment_target_not_supported)
{
    expect_parse_error(L"a, b = 1, 2\n",
                       "SyntaxError: assignment target must be a simple "
                       "variable, attribute, or subscript at offset 0 (line 1, "
                       "column 1), near "
                       "\"a, b = 1, 2\"");
}

TEST(Parser, expression_assignment_target_not_supported)
{
    expect_parse_error(L"a + b = 1\n",
                       "SyntaxError: assignment target must be a simple "
                       "variable, attribute, or subscript at offset 0 (line 1, "
                       "column 1), near "
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
