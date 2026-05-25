#include "code_object.h"
#include "code_object_builder.h"
#include "code_object_print.h"
#include "codegen.h"
#include "float.h"
#include "function.h"
#include "module_object.h"
#include "parser.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

using namespace cl;

std::string bytecode_str_from_file(const wchar_t *expr)
{
    test::VmTestContext test_context;
    CodeObject *code_obj = test_context.compile_file(expr);
    std::string actual = fmt::to_string(*code_obj);
    return actual;
}

std::string bytecode_str_from_interactive(const wchar_t *expr)
{
    test::VmTestContext test_context;
    CodeObject *code_obj =
        test_context.thread()->compile(expr, StartRule::Interactive);
    return fmt::to_string(*code_obj);
}

std::string trusted_builtin_bytecode_str_from_file(const wchar_t *expr)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> module_name =
        test_context.vm().get_or_create_interned_string_value(
            L"<test-builtin>");
    ModuleObject *module = test_context.make_test_module_object(
        module_name, test_context.vm().global_builtins_module().raw_value());
    CodeObject *code_obj = test_context.thread()->compile_in_module(
        expr, StartRule::File, module, LanguageMode::TrustedCloverExtensions);
    return fmt::to_string(*code_obj);
}

// Keep this file intentionally small and structural. Interpreter tests own
// most semantic coverage; codegen tests should pin down bytecode shapes that
// matter for lowering strategy or calling conventions.

TEST(Codegen, simple2)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 LeftShiftSmi 4\n"
                           "    4 AddSmi 3\n"
                           "    6 Return\n";
    std::string actual = bytecode_str_from_file(L"(1 << 4) + 3");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, assignment2)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 4\n"
        "    2 StaGlobal c[0], module_global_mutation_ic[0]\n"
        "    5 LdaGlobal c[0], module_global_read_ic[0]\n"
        "    8 AddSmi 7\n"
        "   10 StaGlobal c[0], module_global_mutation_ic[1]\n"
        "   13 Return\n"
        "Constant 0: \"a\"\n";
    std::string actual = bytecode_str_from_file(L"a = 4\n"
                                                "a += 7\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, interactive_expression_returns_value)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 AddSmi 2\n"
                           "    4 Return\n";
    std::string actual = bytecode_str_from_interactive(L"1 + 2\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, interactive_assignment_returns_none)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 4\n"
        "    2 StaGlobal c[0], module_global_mutation_ic[0]\n"
        "    5 LdaNone\n"
        "    6 Return\n"
        "Constant 0: \"a\"\n";
    std::string actual = bytecode_str_from_interactive(L"a = 4\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, empty_file_returns_none)
{
    std::string expected = "Code object:\n"
                           "    0 LdaNone\n"
                           "    1 Return\n";
    std::string actual = bytecode_str_from_file(L"");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, bytecode_constant_index_overflow_throws)
{
    std::wstring source;
    for(uint32_t idx = 0; idx < 257; ++idx)
    {
        source += L"a";
        source += std::to_wstring(idx);
        source += L" = 0\n";
    }

    EXPECT_THROW(bytecode_str_from_file(source.c_str()), std::runtime_error);
}

TEST(Codegen, unresolved_jump_target_does_not_mask_codegen_error)
{
    std::wstring source;
    for(uint32_t idx = 0; idx < 256; ++idx)
    {
        source += L"a";
        source += std::to_wstring(idx);
        source += L" = 0\n";
    }
    source += L"assert 1.5\n";

    try
    {
        (void)bytecode_str_from_file(source.c_str());
        FAIL() << "Expected constant table overflow";
    }
    catch(const std::runtime_error &err)
    {
        EXPECT_STREQ("constant table index out of range", err.what());
    }
}

TEST(Codegen, code_object_builder_reuses_duplicate_constants)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> module_name =
        test_context.vm().get_or_create_interned_string_value(L"module");
    ModuleObject *module = test_context.make_test_module_object(
        module_name, test_context.vm().global_builtins_module().raw_value());
    TValue<String> code_name =
        test_context.vm().get_or_create_interned_string_value(L"code");
    CodeObjectBuilder builder(&test_context.vm(), nullptr,
                              TValue<ModuleObject>::from_oop(module), nullptr,
                              code_name);
    TValue<String> name =
        test_context.vm().get_or_create_interned_string_value(L"name");

    EXPECT_EQ(0u, builder.allocate_constant(name));
    EXPECT_EQ(0u, builder.allocate_constant(name));
    EXPECT_EQ(1u, builder.allocate_constant(Value::from_smi(7)));
    EXPECT_EQ(1u, builder.allocate_constant(Value::from_smi(7)));

    CodeObject *code_obj = builder.finalize();
    EXPECT_EQ(size_t(2), code_obj->constant_table.size());
}

TEST(Codegen, bytecode_cache_index_overflow_throws)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    TValue<String> module_name =
        test_context.vm().get_or_create_interned_string_value(L"module");
    ModuleObject *module = test_context.make_test_module_object(
        module_name, test_context.vm().global_builtins_module().raw_value());
    TValue<String> code_name =
        test_context.vm().get_or_create_interned_string_value(L"code");
    CodeObjectBuilder builder(&test_context.vm(), nullptr,
                              TValue<ModuleObject>::from_oop(module), nullptr,
                              code_name);
    uint32_t name_idx = builder.allocate_constant(
        test_context.vm().get_or_create_interned_string_value(L"name"));
    ASSERT_EQ(0u, name_idx);

    for(uint32_t idx = 0; idx < 256; ++idx)
    {
        builder.emit_lda_global(0, uint8_t(name_idx));
    }
    EXPECT_THROW(builder.emit_lda_global(0, uint8_t(name_idx)),
                 std::runtime_error);
}

TEST(Codegen, relative_import_level_overflow_throws)
{
    std::wstring source = L"from ";
    source.append(256, L'.');
    source += L"pkg import value\n";

    EXPECT_THROW(bytecode_str_from_file(source.c_str()), std::runtime_error);
}

TEST(Codegen, import_statement_uses_import_name_and_normal_store)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaNone\n"
        "    1 ImportName c[0], 0\n"
        "    4 StaGlobal c[0], module_global_mutation_ic[0]\n"
        "    7 Return\n"
        "Constant 0: \"assignment\"\n";
    std::string actual = bytecode_str_from_file(L"import assignment\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, multiple_import_statement_loops_over_aliases)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaNone\n"
        "    1 ImportName c[0], 0\n"
        "    4 StaGlobal c[0], module_global_mutation_ic[0]\n"
        "    7 LdaNone\n"
        "    8 ImportName c[1], 0\n"
        "   11 ImportFrom c[2]\n"
        "   13 StaGlobal c[3], module_global_mutation_ic[1]\n"
        "   16 Return\n"
        "Constant 0: \"assignment\"\n"
        "Constant 1: \"pkg.mod\"\n"
        "Constant 2: \"mod\"\n"
        "Constant 3: \"alias\"\n";
    std::string actual =
        bytecode_str_from_file(L"import assignment, pkg.mod as alias\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, dotted_import_statement_imports_full_name_and_stores_head)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaNone\n"
        "    1 ImportName c[0], 0\n"
        "    4 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    7 Return\n"
        "Constant 0: \"pkg.mod\"\n"
        "Constant 1: \"pkg\"\n";
    std::string actual = bytecode_str_from_file(L"import pkg.mod\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, from_import_statement_uses_fromlist_and_import_from)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaConstant c[0]\n"
        "    2 ImportName c[1], 0\n"
        "    5 Star0\n"
        "    6 Ldar0\n"
        "    7 ImportFrom c[2]\n"
        "    9 StaGlobal c[2], module_global_mutation_ic[0]\n"
        "   12 Ldar0\n"
        "   13 ImportFrom c[3]\n"
        "   15 StaGlobal c[4], module_global_mutation_ic[1]\n"
        "   18 Return\n"
        "Constant 0: (\"marker\", \"value\")\n"
        "Constant 1: \"assignment\"\n"
        "Constant 2: \"marker\"\n"
        "Constant 3: \"value\"\n"
        "Constant 4: \"alias\"\n";
    std::string actual = bytecode_str_from_file(
        L"from assignment import marker, value as alias\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, parenthesized_from_import_statement_matches_flat_form)
{
    std::string flat = bytecode_str_from_file(
        L"from assignment import marker, value as alias\n");
    std::string parenthesized =
        bytecode_str_from_file(L"from assignment import (\n"
                               L"    marker,\n"
                               L"    value as alias,\n"
                               L")\n");

    EXPECT_EQ(flat, parenthesized);
}

TEST(Codegen, star_from_import_statement_uses_import_star_intrinsic)
{
    std::string expected = "Code object:\n"
                           "    0 LdaConstant c[0]\n"
                           "    2 ImportName c[1], 0\n"
                           "    5 CallRuntimeIntrinsic0 ImportStar\n"
                           "    7 Return\n"
                           "Constant 0: (\"*\",)\n"
                           "Constant 1: \"assignment\"\n";
    std::string actual = bytecode_str_from_file(L"from assignment import *\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, relative_from_import_statement_emits_level)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaConstant c[0]\n"
        "    2 ImportName c[1], 1\n"
        "    5 ImportFrom c[2]\n"
        "    7 StaGlobal c[2], module_global_mutation_ic[0]\n"
        "   10 Return\n"
        "Constant 0: (\"marker\",)\n"
        "Constant 1: \"assignment\"\n"
        "Constant 2: \"marker\"\n";
    std::string actual =
        bytecode_str_from_file(L"from .assignment import marker\n");

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

    std::string expected =
        "Code object:\n"
        "    0 LdaGlobal c[0], module_global_read_ic[0]\n"
        "    3 JumpIfFalse 14\n"
        "    6 LdaSmi 1\n"
        "    8 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "   11 Jump 33\n"
        "   14 LdaGlobal c[2], module_global_read_ic[1]\n"
        "   17 JumpIfFalse 28\n"
        "   20 LdaSmi 2\n"
        "   22 StaGlobal c[1], module_global_mutation_ic[1]\n"
        "   25 Jump 33\n"
        "   28 LdaSmi 3\n"
        "   30 StaGlobal c[1], module_global_mutation_ic[2]\n"
        "   33 LdaGlobal c[1], module_global_read_ic[2]\n"
        "   36 Return\n"
        "Constant 0: \"a\"\n"
        "Constant 1: \"b\"\n"
        "Constant 2: \"c\"\n";
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

    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 2\n"
        "    2 StaGlobal c[0], module_global_mutation_ic[0]\n"
        "    5 LdaSmi 0\n"
        "    7 StaGlobal c[1], module_global_mutation_ic[1]\n"
        "   10 LdaGlobal c[0], module_global_read_ic[0]\n"
        "   13 JumpIfFalse 30\n"
        "   16 LdaGlobal c[0], module_global_read_ic[1]\n"
        "   19 SubSmi 1\n"
        "   21 StaGlobal c[0], module_global_mutation_ic[2]\n"
        "   24 LdaGlobal c[0], module_global_read_ic[2]\n"
        "   27 JumpIfTrue 16\n"
        "   30 LdaSmi 7\n"
        "   32 StaGlobal c[1], module_global_mutation_ic[3]\n"
        "   35 LdaGlobal c[1], module_global_read_ic[3]\n"
        "   38 Return\n"
        "Constant 0: \"a\"\n"
        "Constant 1: \"b\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, function_multiple_parameters)
{
    const wchar_t *test_case = L"def add3(a, b, c):\n"
                               "    return a + b + c\n"
                               "add3(1, 2, 3)\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 LdaGlobal c[1], module_global_read_ic[0]\n"
        "    8 Star0\n"
        "    9 LdaSmi 1\n"
        "   11 Star a0\n"
        "   13 LdaSmi 2\n"
        "   15 Star a1\n"
        "   17 LdaSmi 3\n"
        "   19 Star a2\n"
        "   21 CallSimple r0, {a0..a2}, call_ic[0]\n"
        "   26 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p1\n"
        "    2 Add p0\n"
        "    4 Star0\n"
        "    5 Ldar p2\n"
        "    7 Add r0\n"
        "    9 Return\n"
        "   10 LdaNone\n"
        "   11 Return\n"
        "\n"
        "Constant 1: \"add3\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, function_defaults_use_create_function_with_defaults)
{
    const wchar_t *test_case = L"def f(a, b=2):\n"
                               "    return a + b\n"
                               "f(1)\n";

    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 2\n"
        "    2 Star0\n"
        "    3 CreateTuple {r0:1}\n"
        "    6 Star1\n"
        "    7 CreateFunctionWithDefaults c[0], r1\n"
        "   10 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "   13 LdaGlobal c[1], module_global_read_ic[0]\n"
        "   16 Star0\n"
        "   17 LdaSmi 1\n"
        "   19 Star a0\n"
        "   21 CallSimple r0, {a0:1}, call_ic[0]\n"
        "   26 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p1\n"
        "    2 Add p0\n"
        "    4 Return\n"
        "    5 LdaNone\n"
        "    6 Return\n"
        "\n"
        "Constant 1: \"f\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, function_varargs_parameter_layout)
{
    test::VmTestContext test_context;
    CodeObject *module_code =
        test_context.compile_file(L"def f(a, b=1, *args):\n"
                                  L"    return args\n");
    CodeObject *function_code =
        module_code->constant_table[0].value().get_ptr<CodeObject>();

    EXPECT_EQ(3, function_code->function_signature.n_parameters);
    EXPECT_EQ(2, function_code->function_signature.n_positional_parameters);
    EXPECT_EQ(2, function_code->function_signature.n_pos_or_kw_parameters);
    EXPECT_TRUE(function_code->has_varargs());
    EXPECT_EQ(7, function_code->get_highest_occupied_frame_offset());
}

TEST(Codegen, function_copies_call_signature_from_code_object)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *module_code =
        test_context.compile_file(L"def f(a, b=1, *args):\n"
                                  L"    return args\n"
                                  L"f\n");

    Value function_value =
        test_context.thread()->run_clovervm_code_object(module_code);
    ASSERT_TRUE(can_convert_to<Function>(function_value));
    Function *function = assume_convert_to<Function>(function_value);
    CodeObject *function_code = function->code_object.extract();

    EXPECT_EQ(function_code->function_signature.n_parameters,
              function->call_signature.function.n_parameters);
    EXPECT_EQ(function_code->function_signature.n_positional_parameters,
              function->call_signature.function.n_positional_parameters);
    EXPECT_EQ(function_code->function_signature.n_pos_or_kw_parameters,
              function->call_signature.function.n_pos_or_kw_parameters);
    EXPECT_EQ(function_code->function_signature.parameter_flags,
              function->call_signature.function.parameter_flags);
    EXPECT_EQ(1u, function->call_signature.min_positional_arity);
    EXPECT_EQ(Function::VarArgs, function->call_signature.max_positional_arity);
}

TEST(Codegen, parameter_frame_offsets_are_padded_to_abi_alignment)
{
    test::VmTestContext test_context;
    CodeObject *one_param = test_context.compile_file(L"def f(a):\n"
                                                      L"    return a\n");
    CodeObject *two_params = test_context.compile_file(L"def f(a, b):\n"
                                                       L"    return a\n");
    CodeObject *three_params = test_context.compile_file(L"def f(a, b, c):\n"
                                                         L"    return a\n");
    CodeObject *four_params = test_context.compile_file(L"def f(a, b, c, d):\n"
                                                        L"    return a\n");

    EXPECT_EQ(5, one_param->constant_table[0]
                     .value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(5, two_params->constant_table[0]
                     .value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(7, three_params->constant_table[0]
                     .value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(7, four_params->constant_table[0]
                     .value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
}

TEST(Codegen, binary_expression_reuses_local_register_operand)
{
    const wchar_t *test_case = L"def add(a, b):\n"
                               "    return a + b\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p1\n"
        "    2 Add p0\n"
        "    4 Return\n"
        "    5 LdaNone\n"
        "    6 Return\n"
        "\n"
        "Constant 1: \"add\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, comparison_reuses_local_register_operand)
{
    const wchar_t *test_case = L"def lt(a, b):\n"
                               "    return a < b\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p1\n"
        "    2 TestLess p0\n"
        "    4 Return\n"
        "    5 LdaNone\n"
        "    6 Return\n"
        "\n"
        "Constant 1: \"lt\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, assert_statement_uses_explicit_failure_path)
{
    std::string expected = "Code object:\n"
                           "    0 LdaFalse\n"
                           "    1 JumpIfTrue 5\n"
                           "    4 RaiseAssertionError\n"
                           "    5 Return\n";
    std::string actual = bytecode_str_from_file(L"assert False\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, assert_statement_message_is_only_evaluated_on_failure)
{
    std::string expected = "Code object:\n"
                           "    0 LdaFalse\n"
                           "    1 JumpIfTrue 7\n"
                           "    4 LdaConstant c[0]\n"
                           "    6 RaiseAssertionErrorWithMessage\n"
                           "    7 Return\n"
                           "Constant 0: \"lol\"\n";
    std::string actual = bytecode_str_from_file(L"assert False, \"lol\"\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, raise_statement_evaluates_expression_and_unwinds)
{
    std::string expected = "Code object:\n"
                           "    0 LdaGlobal c[0], module_global_read_ic[0]\n"
                           "    3 RaiseUnwind\n"
                           "    4 Return\n"
                           "Constant 0: \"ValueError\"\n";
    std::string actual = bytecode_str_from_file(L"raise ValueError\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, try_bare_except_emits_exception_table)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaGlobal c[0], module_global_read_ic[0]\n"
        "    3 RaiseUnwind\n"
        "    4 Jump 16\n"
        "    7 ClearActiveException\n"
        "    8 LdaSmi 7\n"
        "   10 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "   13 Jump 16\n"
        "   16 LdaGlobal c[1], module_global_read_ic[1]\n"
        "   19 Return\n"
        "Exception table:\n"
        "    0..4 -> 7\n"
        "Constant 0: \"ValueError\"\n"
        "Constant 1: \"result\"\n";
    std::string actual = bytecode_str_from_file(L"try:\n"
                                                L"    raise ValueError\n"
                                                L"except:\n"
                                                L"    result = 7\n"
                                                L"result\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, try_typed_except_checks_active_exception)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaGlobal c[0], module_global_read_ic[0]\n"
        "    3 RaiseUnwind\n"
        "    4 Jump 24\n"
        "    7 LdaGlobal c[1], module_global_read_ic[1]\n"
        "   10 ActiveExceptionIsInstance\n"
        "   11 JumpIfFalse 23\n"
        "   14 ClearActiveException\n"
        "   15 LdaSmi 7\n"
        "   17 StaGlobal c[2], module_global_mutation_ic[0]\n"
        "   20 Jump 24\n"
        "   23 ReraiseActiveException\n"
        "   24 LdaGlobal c[2], module_global_read_ic[2]\n"
        "   27 Return\n"
        "Exception table:\n"
        "    0..4 -> 7\n"
        "Constant 0: \"ValueError\"\n"
        "Constant 1: \"Exception\"\n"
        "Constant 2: \"result\"\n";
    std::string actual = bytecode_str_from_file(L"try:\n"
                                                L"    raise ValueError\n"
                                                L"except Exception:\n"
                                                L"    result = 7\n"
                                                L"result\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, bare_raise_in_handler_uses_saved_exception_register)
{
    std::string expected = "Code object:\n"
                           "    0 LdaGlobal c[0], module_global_read_ic[0]\n"
                           "    3 RaiseUnwind\n"
                           "    4 Jump 22\n"
                           "    7 LdaGlobal c[1], module_global_read_ic[1]\n"
                           "   10 ActiveExceptionIsInstance\n"
                           "   11 JumpIfFalse 21\n"
                           "   14 DrainActiveExceptionInto r0\n"
                           "   16 Ldar0\n"
                           "   17 RaiseUnwind\n"
                           "   18 Jump 22\n"
                           "   21 ReraiseActiveException\n"
                           "   22 Return\n"
                           "Exception table:\n"
                           "    0..4 -> 7\n"
                           "Constant 0: \"ValueError\"\n"
                           "Constant 1: \"Exception\"\n";
    std::string actual = bytecode_str_from_file(L"try:\n"
                                                L"    raise ValueError\n"
                                                L"except Exception:\n"
                                                L"    raise\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, non_bare_raise_in_handler_uses_saved_context_register)
{
    std::string expected = "Code object:\n"
                           "    0 LdaGlobal c[0], module_global_read_ic[0]\n"
                           "    3 RaiseUnwind\n"
                           "    4 Jump 25\n"
                           "    7 LdaGlobal c[1], module_global_read_ic[1]\n"
                           "   10 ActiveExceptionIsInstance\n"
                           "   11 JumpIfFalse 24\n"
                           "   14 DrainActiveExceptionInto r0\n"
                           "   16 LdaGlobal c[2], module_global_read_ic[2]\n"
                           "   19 RaiseUnwindWithContext r0\n"
                           "   21 Jump 25\n"
                           "   24 ReraiseActiveException\n"
                           "   25 Return\n"
                           "Exception table:\n"
                           "    0..4 -> 7\n"
                           "Constant 0: \"NameError\"\n"
                           "Constant 1: \"Exception\"\n"
                           "Constant 2: \"ValueError\"\n";
    std::string actual = bytecode_str_from_file(L"try:\n"
                                                L"    raise NameError\n"
                                                L"except Exception:\n"
                                                L"    raise ValueError\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, try_finally_emits_normal_and_exceptional_cleanup_paths)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 1\n"
        "    2 StaGlobal c[0], module_global_mutation_ic[0]\n"
        "    5 LdaSmi 2\n"
        "    7 StaGlobal c[0], module_global_mutation_ic[1]\n"
        "   10 Jump 22\n"
        "   13 DrainActiveExceptionInto r0\n"
        "   15 LdaSmi 2\n"
        "   17 StaGlobal c[0], module_global_mutation_ic[2]\n"
        "   20 Ldar0\n"
        "   21 RaiseUnwind\n"
        "   22 LdaGlobal c[0], module_global_read_ic[0]\n"
        "   25 Return\n"
        "Exception table:\n"
        "    0..5 -> 13\n"
        "Constant 0: \"result\"\n";
    std::string actual = bytecode_str_from_file(L"try:\n"
                                                L"    result = 1\n"
                                                L"finally:\n"
                                                L"    result = 2\n"
                                                L"result\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, except_as_without_bare_raise_drains_directly_to_local)
{
    std::string actual = bytecode_str_from_file(L"def f():\n"
                                                L"    try:\n"
                                                L"        raise NameError\n"
                                                L"    except NameError as e:\n"
                                                L"        pass\n");

    EXPECT_NE(std::string::npos, actual.find("DrainActiveExceptionInto r0\n"
                                             "   16 Jump"));
}

TEST(Codegen, except_as_with_bare_raise_keeps_hidden_original)
{
    std::string actual = bytecode_str_from_file(L"def f():\n"
                                                L"    try:\n"
                                                L"        raise NameError\n"
                                                L"    except NameError as e:\n"
                                                L"        e = TypeError\n"
                                                L"        raise\n");

    EXPECT_NE(std::string::npos, actual.find("DrainActiveExceptionInto r1\n"
                                             "   16 Ldar1\n"
                                             "   17 Star0"));
    EXPECT_NE(std::string::npos, actual.find("   22 Ldar1\n"
                                             "   23 RaiseUnwind"));
}

TEST(Codegen, bare_raise_outside_handler_is_runtime_reraise)
{
    std::string expected = "Code object:\n"
                           "    0 RaiseBare\n"
                           "    1 Return\n";
    std::string actual = bytecode_str_from_file(L"raise\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, function_implicit_return_none)
{
    const wchar_t *test_case = L"def f():\n"
                               "    a = 1\n"
                               "f()\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 LdaGlobal c[1], module_global_read_ic[0]\n"
        "    8 Star0\n"
        "    9 CallSimple r0, {a0:0}, call_ic[0]\n"
        "   14 Return\n"
        "Constant 0: Code object:\n"
        "    0 LdaSmi 1\n"
        "    2 Star0\n"
        "    3 LdaNone\n"
        "    4 Return\n"
        "\n"
        "Constant 1: \"f\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, class_definition_passes_hidden_name_and_bases_to_create_class)
{
    const wchar_t *test_case = L"class Cls:\n"
                               "    pass\n"
                               "Cls\n";

    std::string expected =
        "Code object:\n"
        "    0 LdaConstant c[1]\n"
        "    2 Star a0\n"
        "    4 LdaConstant c[2]\n"
        "    6 Star0\n"
        "    7 CreateTuple {r0:1}\n"
        "   10 Star a1\n"
        "   12 CreateClass c[0], a0\n"
        "   15 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "   18 LdaGlobal c[1], module_global_read_ic[0]\n"
        "   21 Return\n"
        "Constant 0: Code object:\n"
        "    0 BuildClass\n"
        "\n"
        "Constant 1: \"Cls\"\n"
        "Constant 2: <class object>\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, string_literal_constant_value)
{
    test::VmTestContext test_context;
    CodeObject *code_obj = test_context.compile_file(L"\"abc\"\n");

    ASSERT_EQ(size_t(1), code_obj->constant_table.size());
    EXPECT_STREQ(L"abc", string_as_wchar_t(TValue<String>::from_value_assumed(
                             code_obj->constant_table[0].value())));

    std::string expected = "Code object:\n"
                           "    0 LdaConstant c[0]\n"
                           "    2 Return\n"
                           "Constant 0: \"abc\"\n";
    EXPECT_EQ(expected, fmt::to_string(*code_obj));
}

TEST(Codegen, float_literal_constant_value)
{
    test::VmTestContext test_context;
    CodeObject *code_obj = test_context.compile_file(L"1.5\n");

    ASSERT_EQ(size_t(1), code_obj->constant_table.size());
    Value constant = code_obj->constant_table[0].value();
    ASSERT_TRUE(can_convert_to<Float>(constant));
    EXPECT_DOUBLE_EQ(1.5, constant.get_ptr<Float>()->value);
}

TEST(Codegen, attribute_load_uses_register_receiver)
{
    const wchar_t *test_case = L"def get(obj):\n"
                               "    return obj.value\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 LoadAttr p0, c[0], read_ic[0]\n"
        "    4 Return\n"
        "    5 LdaNone\n"
        "    6 Return\n"
        "Constant 0: \"value\"\n"
        "\n"
        "Constant 1: \"get\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, attribute_store_uses_register_receiver_and_accumulator_value)
{
    const wchar_t *test_case = L"def set(obj, value):\n"
                               "    obj.value = value\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p1\n"
        "    2 StoreAttr p0, c[0], mutation_ic[0]\n"
        "    6 LdaNone\n"
        "    7 Return\n"
        "Constant 0: \"value\"\n"
        "\n"
        "Constant 1: \"set\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, attribute_delete_uses_register_receiver_and_mutation_cache)
{
    const wchar_t *test_case = L"def clear(obj):\n"
                               "    del obj.value\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 DelAttr p0, c[0], mutation_ic[0]\n"
        "    4 LdaNone\n"
        "    5 Return\n"
        "Constant 0: \"value\"\n"
        "\n"
        "Constant 1: \"clear\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, global_variable_delete_uses_binding_slot)
{
    const wchar_t *test_case = L"value = 1\n"
                               "del value\n";

    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 1\n"
        "    2 StaGlobal c[0], module_global_mutation_ic[0]\n"
        "    5 DelGlobal c[0]\n"
        "    7 Return\n"
        "Constant 0: \"value\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, direct_method_call_uses_callmethodattr)
{
    const wchar_t *test_case = L"def invoke(obj, value):\n"
                               "    return obj.method(value)\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p0\n"
        "    2 Star a0\n"
        "    4 Ldar p1\n"
        "    6 Star a1\n"
        "    8 CallMethodAttr a0, c[0], read_ic[0], call_ic[0], 1\n"
        "   14 Return\n"
        "   15 LdaNone\n"
        "   16 Return\n"
        "Constant 0: \"method\"\n"
        "\n"
        "Constant 1: \"invoke\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, list_literal_uses_createlist_with_contiguous_register_range)
{
    const wchar_t *test_case = L"[1, 2, 4]\n";

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 Star0\n"
                           "    3 LdaSmi 2\n"
                           "    5 Star1\n"
                           "    6 LdaSmi 4\n"
                           "    8 Star2\n"
                           "    9 CreateList {r0..r2}\n"
                           "   12 Return\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, tuple_literal_uses_createtuple_with_contiguous_register_range)
{
    const wchar_t *test_case = L"1, 2, 4\n";

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 Star0\n"
                           "    3 LdaSmi 2\n"
                           "    5 Star1\n"
                           "    6 LdaSmi 4\n"
                           "    8 Star2\n"
                           "    9 CreateTuple {r0..r2}\n"
                           "   12 Return\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, parenthesized_tuple_literal_uses_createtuple)
{
    const wchar_t *test_case = L"(1, 2, 4)\n";

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 Star0\n"
                           "    3 LdaSmi 2\n"
                           "    5 Star1\n"
                           "    6 LdaSmi 4\n"
                           "    8 Star2\n"
                           "    9 CreateTuple {r0..r2}\n"
                           "   12 Return\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, empty_tuple_literal_uses_createtuple)
{
    const wchar_t *test_case = L"()\n";

    std::string expected = "Code object:\n"
                           "    0 CreateTuple {r0:0}\n"
                           "    3 Return\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, dict_literal_uses_createdict_with_contiguous_register_pairs)
{
    const wchar_t *test_case = L"{1: 2, 4: 8}\n";

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 Star0\n"
                           "    3 LdaSmi 2\n"
                           "    5 Star1\n"
                           "    6 LdaSmi 4\n"
                           "    8 Star2\n"
                           "    9 LdaSmi 8\n"
                           "   11 Star3\n"
                           "   12 CreateDict {r0..r3}\n"
                           "   15 Return\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, subscript_load_uses_receiver_register_and_accumulator_key)
{
    const wchar_t *test_case = L"def get(obj, idx):\n"
                               L"    return obj[idx]\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p1\n"
        "    2 LoadSubscript p0\n"
        "    4 Return\n"
        "    5 LdaNone\n"
        "    6 Return\n"
        "\n"
        "Constant 1: \"get\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, subscript_store_uses_receiver_and_key_registers)
{
    const wchar_t *test_case = L"def set(obj, idx, value):\n"
                               L"    obj[idx] = value\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p2\n"
        "    2 StoreSubscript p0, p1\n"
        "    5 LdaNone\n"
        "    6 Return\n"
        "\n"
        "Constant 1: \"set\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, subscript_delete_uses_receiver_and_key_registers)
{
    const wchar_t *test_case = L"def clear(obj, idx):\n"
                               L"    del obj[idx]\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 DelSubscript p0, p1\n"
        "    3 LdaNone\n"
        "    4 Return\n"
        "\n"
        "Constant 1: \"clear\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, subscript_augmented_assignment_evaluates_receiver_and_key_once)
{
    const wchar_t *test_case = L"def bump(obj, idx):\n"
                               L"    obj[idx] += 1\n";

    std::string expected =
        "Code object:\n"
        "    0 CreateFunction c[0]\n"
        "    2 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "    5 Return\n"
        "Constant 0: Code object:\n"
        "    0 Ldar p1\n"
        "    2 LoadSubscript p0\n"
        "    4 AddSmi 1\n"
        "    6 StoreSubscript p0, p1\n"
        "    9 LdaNone\n"
        "   10 Return\n"
        "\n"
        "Constant 1: \"bump\"\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, direct_range_for_loop_uses_specialized_fast_path_with_fallback)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaSmi 0\n"
        "    2 StaGlobal c[0], module_global_mutation_ic[0]\n"
        "    5 LdaGlobal c[1], module_global_read_ic[0]\n"
        "    8 Star0\n"
        "    9 LdaSmi 3\n"
        "   11 Star1\n"
        "   12 ForPrepRange1 r0, 38\n"
        "   16 ForIterRange1 r0, 97\n"
        "   20 StaGlobal c[2], module_global_mutation_ic[1]\n"
        "   23 LdaGlobal c[0], module_global_read_ic[1]\n"
        "   26 Star3\n"
        "   27 LdaGlobal c[2], module_global_read_ic[2]\n"
        "   30 Add r3\n"
        "   32 StaGlobal c[0], module_global_mutation_ic[2]\n"
        "   35 Jump 16\n"
        "   38 Ldar1\n"
        "   39 Star a0\n"
        "   41 CallSimple r0, {a0:1}, call_ic[0]\n"
        "   46 Star a0\n"
        "   48 CallSpecialMethod a0, c[3], read_ic[0], call_ic[1], 0, c[4], "
        "c[5]\n"
        "   56 Star2\n"
        "   57 Ldar2\n"
        "   58 Star a0\n"
        "   60 CallSpecialMethod a0, c[6], read_ic[1], call_ic[2], 0, c[4], "
        "c[7]\n"
        "   68 StaGlobal c[2], module_global_mutation_ic[3]\n"
        "   71 LdaGlobal c[0], module_global_read_ic[3]\n"
        "   74 Star3\n"
        "   75 LdaGlobal c[2], module_global_read_ic[4]\n"
        "   78 Add r3\n"
        "   80 StaGlobal c[0], module_global_mutation_ic[4]\n"
        "   83 Jump 57\n"
        "   86 LdaConstant c[8]\n"
        "   88 ActiveExceptionIsInstance\n"
        "   89 JumpIfFalse 96\n"
        "   92 ClearActiveException\n"
        "   93 Jump 97\n"
        "   96 ReraiseActiveException\n"
        "   97 LdaGlobal c[0], module_global_read_ic[5]\n"
        "  100 Return\n"
        "Exception table:\n"
        "    60..68 -> 86\n"
        "Constant 0: \"total\"\n"
        "Constant 1: \"range\"\n"
        "Constant 2: \"x\"\n"
        "Constant 3: \"__iter__\"\n"
        "Constant 4: <class TypeError>\n"
        "Constant 5: \"object is not iterable\"\n"
        "Constant 6: \"__next__\"\n"
        "Constant 7: \"object is not an iterator\"\n"
        "Constant 8: <class StopIteration>\n";
    std::string actual = bytecode_str_from_file(L"total = 0\n"
                                                "for x in range(3):\n"
                                                "    total += x\n"
                                                "total\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, local_augmented_assignment_in_loop_uses_dominating_binding)
{
    std::string actual = bytecode_str_from_file(L"def sum_range(n):\n"
                                                L"    acc = 0\n"
                                                L"    for i in range(n):\n"
                                                L"        acc += i\n"
                                                L"    return acc\n");

    EXPECT_NE(std::string::npos, actual.find("   19 Ldar1\n"
                                             "   20 Add r0\n"
                                             "   22 Star0\n"));
    EXPECT_EQ(std::string::npos, actual.find("LoadLocalChecked r0"));
}

TEST(Codegen, local_deleted_in_loop_still_uses_checked_load)
{
    std::string actual = bytecode_str_from_file(L"def read_after_delete(n):\n"
                                                L"    x = 1\n"
                                                L"    for i in range(n):\n"
                                                L"        x\n"
                                                L"        del x\n"
                                                L"    return 0\n");

    EXPECT_NE(std::string::npos, actual.find("LoadLocalChecked r0"));
}

TEST(Codegen, local_deleted_in_while_body_checks_next_condition_load)
{
    std::string actual = bytecode_str_from_file(L"def read_after_delete():\n"
                                                L"    x = 1\n"
                                                L"    while x:\n"
                                                L"        del x\n"
                                                L"    return 0\n");

    EXPECT_NE(std::string::npos, actual.find("LoadLocalChecked r0"));
}

TEST(Codegen, non_direct_for_loop_uses_generic_iterator_protocol_calls)
{
    std::string expected =
        "Code object:\n"
        "    0 LdaGlobal c[0], module_global_read_ic[0]\n"
        "    3 Star0\n"
        "    4 LdaSmi 3\n"
        "    6 Star a0\n"
        "    8 CallSimple r0, {a0:1}, call_ic[0]\n"
        "   13 StaGlobal c[1], module_global_mutation_ic[0]\n"
        "   16 LdaGlobal c[1], module_global_read_ic[1]\n"
        "   19 Star a0\n"
        "   21 CallSpecialMethod a0, c[2], read_ic[0], call_ic[1], 0, c[3], "
        "c[4]\n"
        "   29 Star0\n"
        "   30 Ldar0\n"
        "   31 Star a0\n"
        "   33 CallSpecialMethod a0, c[5], read_ic[1], call_ic[2], 0, c[3], "
        "c[6]\n"
        "   41 StaGlobal c[8], module_global_mutation_ic[1]\n"
        "   44 LdaGlobal c[8], module_global_read_ic[2]\n"
        "   47 Jump 30\n"
        "   50 LdaConstant c[7]\n"
        "   52 ActiveExceptionIsInstance\n"
        "   53 JumpIfFalse 60\n"
        "   56 ClearActiveException\n"
        "   57 Jump 61\n"
        "   60 ReraiseActiveException\n"
        "   61 Return\n"
        "Exception table:\n"
        "    33..41 -> 50\n"
        "Constant 0: \"range\"\n"
        "Constant 1: \"it\"\n"
        "Constant 2: \"__iter__\"\n"
        "Constant 3: <class TypeError>\n"
        "Constant 4: \"object is not iterable\"\n"
        "Constant 5: \"__next__\"\n"
        "Constant 6: \"object is not an iterator\"\n"
        "Constant 7: <class StopIteration>\n"
        "Constant 8: \"x\"\n";
    std::string actual = bytecode_str_from_file(L"it = range(3)\n"
                                                "for x in it:\n"
                                                "    x\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, trusted_clover_call_special_lowers_to_special_method_call)
{
    std::string actual = trusted_builtin_bytecode_str_from_file(
        L"def call_repr(obj):\n"
        L"    return __clover_call_special__(obj, \"__repr__\", TypeError, "
        L"\"missing repr\")\n");

    EXPECT_NE(std::string::npos, actual.find("CallSpecialMethod"));
    EXPECT_NE(std::string::npos, actual.find("\"__repr__\""));
    EXPECT_NE(std::string::npos, actual.find("<class TypeError>"));
    EXPECT_NE(std::string::npos, actual.find("\"missing repr\""));
}

TEST(Codegen, user_clover_call_special_name_is_ordinary_call)
{
    std::string actual = bytecode_str_from_file(
        L"def call_repr(obj):\n"
        L"    return __clover_call_special__(obj, \"__repr__\", TypeError, "
        L"\"missing repr\")\n");

    EXPECT_EQ(std::string::npos, actual.find("CallSpecialMethod"));
    EXPECT_NE(std::string::npos, actual.find("CallSimple"));
}

TEST(Codegen, trusted_clover_write_stdout_lowers_to_opcode)
{
    std::string actual = trusted_builtin_bytecode_str_from_file(
        L"def write(value):\n"
        L"    return __clover_write_stdout__(value)\n");

    EXPECT_NE(std::string::npos, actual.find("WriteStdout"));
    EXPECT_EQ(std::string::npos, actual.find("CallSimple"));
}

TEST(Codegen, user_clover_write_stdout_name_is_ordinary_call)
{
    std::string actual =
        bytecode_str_from_file(L"def write(value):\n"
                               L"    return __clover_write_stdout__(value)\n");

    EXPECT_EQ(std::string::npos, actual.find("WriteStdout"));
    EXPECT_NE(std::string::npos, actual.find("CallSimple"));
}

TEST(Codegen, trusted_clover_globals_lowers_to_intrinsic)
{
    std::string actual = trusted_builtin_bytecode_str_from_file(
        L"def read_globals():\n"
        L"    return __clover_globals__()\n");

    EXPECT_NE(std::string::npos, actual.find("CallRuntimeIntrinsic0 Globals"));
    EXPECT_EQ(std::string::npos, actual.find("CallSimple"));
}

TEST(Codegen, user_clover_globals_name_is_ordinary_call)
{
    std::string actual =
        bytecode_str_from_file(L"def read_globals():\n"
                               L"    return __clover_globals__()\n");

    EXPECT_EQ(std::string::npos, actual.find("CallRuntimeIntrinsic0 Globals"));
    EXPECT_NE(std::string::npos, actual.find("CallSimple"));
}

TEST(Codegen, trusted_clover_locals_lowers_to_intrinsic)
{
    std::string actual = trusted_builtin_bytecode_str_from_file(
        L"def read_locals():\n"
        L"    return __clover_locals__()\n");

    EXPECT_NE(std::string::npos, actual.find("CallRuntimeIntrinsic0 Locals"));
    EXPECT_EQ(std::string::npos, actual.find("CallSimple"));
}

TEST(Codegen, user_clover_locals_name_is_ordinary_call)
{
    std::string actual =
        bytecode_str_from_file(L"def read_locals():\n"
                               L"    return __clover_locals__()\n");

    EXPECT_EQ(std::string::npos, actual.find("CallRuntimeIntrinsic0 Locals"));
    EXPECT_NE(std::string::npos, actual.find("CallSimple"));
}

TEST(Codegen, trusted_clover_sqrt_lowers_to_opcode)
{
    std::string actual = trusted_builtin_bytecode_str_from_file(
        L"def root(value):\n"
        L"    return __clover_sqrt__(value)\n");

    EXPECT_NE(std::string::npos, actual.find("Sqrt"));
    EXPECT_EQ(std::string::npos, actual.find("CallSimple"));
}

TEST(Codegen, user_clover_sqrt_name_is_ordinary_call)
{
    std::string actual =
        bytecode_str_from_file(L"def root(value):\n"
                               L"    return __clover_sqrt__(value)\n");

    EXPECT_EQ(std::string::npos, actual.find("Sqrt"));
    EXPECT_NE(std::string::npos, actual.find("CallSimple"));
}
