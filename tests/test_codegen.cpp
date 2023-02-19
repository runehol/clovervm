#include <gtest/gtest.h>
#include "tokenizer.h"
#include "compilation_unit.h"
#include "token_print.h"
#include "parser.h"
#include "codegen.h"
#include "code_object_print.h"
#include <fmt/xchar.h>

using namespace cl;


TEST(Codegen, simple)
{
    CompilationUnit input(L"1 + 2  *  (4 + 3)");
    std::string expected =
        "Code object:\n"
		"00011 LdaSmi 4\n"
		"00013 AddSmi 3\n"
		"00007 MulSmi 2\n"
		"00002 AddSmi 1\n"
		"00000 Return\n";

    TokenVector tv = tokenize(input);
    AstVector av = parse(tv, StartRule::Eval);
    CodeObject code_obj = generate_code(av);
    std::string actual = fmt::to_string(code_obj);
    EXPECT_EQ(expected, actual);
}


TEST(Codegen, simple2)
{
    CompilationUnit input(L"(1 << 4) + 3");
    std::string expected =
        "Code object:\n"
		"00001 LdaSmi 1\n"
		"00003 LeftShiftSmi 4\n"
		"00009 AddSmi 3\n"
		"00000 Return\n";

    TokenVector tv = tokenize(input);
    AstVector av = parse(tv, StartRule::Eval);
    CodeObject code_obj = generate_code(av);
    std::string actual = fmt::to_string(code_obj);
    EXPECT_EQ(expected, actual);
}
