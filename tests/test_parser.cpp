#include <gtest/gtest.h>
#include "virtual_machine.h"
#include "tokenizer.h"
#include "compilation_unit.h"
#include "token_print.h"
#include "parser.h"
#include "ast_print.h"
#include <fmt/xchar.h>

using namespace cl;


TEST(Parser, simple)
{
    VirtualMachine vm;
    CompilationUnit input(L"1 + 2  *  (4 + 3)");
    std::string expected = "1 + 2 * (4 + 3)";

    TokenVector tv = tokenize(input);
    AstVector av = parse(vm, tv, StartRule::Eval);
    std::string actual = fmt::to_string(av);
    EXPECT_EQ(expected, actual);
}


TEST(Parser, simple2)
{
    VirtualMachine vm;
    CompilationUnit input(L"(1 << 4) + 3");
    std::string expected = "(1 << 4) + 3";

    TokenVector tv = tokenize(input);
    AstVector av = parse(vm, tv, StartRule::Eval);
    std::string actual = fmt::to_string(av);
    EXPECT_EQ(expected, actual);
}
