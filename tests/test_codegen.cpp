#include "code_object.h"
#include "code_object_print.h"
#include "parser.h"
#include "startup_wrapper.h"
#include "str.h"
#include "test_helpers.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <fmt/xchar.h>
#include <gtest/gtest.h>

using namespace cl;

std::string bytecode_str_from_file(const wchar_t *expr)
{
    test::VmTestContext test_context;
    CodeObject *code_obj = test_context.compile_file(expr);
    std::string actual = fmt::to_string(*code_obj);
    return actual;
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
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 4\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaGlobal [0]\n"
                           "   12 AddSmi 7\n"
                           "   14 StaGlobal [0]\n"
                           "   19 Return\n";
    std::string actual = bytecode_str_from_file(L"a = 4\n"
                                                "a += 7\n");

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

    std::string expected = "Code object:\n"
                           "    0 LdaGlobal [0]\n"
                           "    5 JumpIfFalse 18\n"
                           "    8 LdaSmi 1\n"
                           "   10 StaGlobal [1]\n"
                           "   15 Jump 43\n"
                           "   18 LdaGlobal [2]\n"
                           "   23 JumpIfFalse 36\n"
                           "   26 LdaSmi 2\n"
                           "   28 StaGlobal [1]\n"
                           "   33 Jump 43\n"
                           "   36 LdaSmi 3\n"
                           "   38 StaGlobal [1]\n"
                           "   43 LdaGlobal [1]\n"
                           "   48 Return\n";
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

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 2\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaSmi 0\n"
                           "    9 StaGlobal [1]\n"
                           "   14 LdaGlobal [0]\n"
                           "   19 JumpIfFalse 42\n"
                           "   22 LdaGlobal [0]\n"
                           "   27 SubSmi 1\n"
                           "   29 StaGlobal [0]\n"
                           "   34 LdaGlobal [0]\n"
                           "   39 JumpIfTrue 22\n"
                           "   42 LdaSmi 7\n"
                           "   44 StaGlobal [1]\n"
                           "   49 LdaGlobal [1]\n"
                           "   54 Return\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, function_multiple_parameters)
{
    const wchar_t *test_case = L"def add3(a, b, c):\n"
                               "    return a + b + c\n"
                               "add3(1, 2, 3)\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaGlobal [0]\n"
                           "   12 Star0\n"
                           "   13 LdaSmi 1\n"
                           "   15 Star a0\n"
                           "   17 LdaSmi 2\n"
                           "   19 Star a1\n"
                           "   21 LdaSmi 3\n"
                           "   23 Star a2\n"
                           "   25 CallSimple r0, {a0..a2}, call_ic[0]\n"
                           "   30 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p1\n"
                           "    2 Add p0\n"
                           "    4 Star0\n"
                           "    5 Ldar p2\n"
                           "    7 Add r0\n"
                           "    9 Return\n"
                           "   10 LdaNone\n"
                           "   11 Return\n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, function_defaults_use_create_function_with_defaults)
{
    const wchar_t *test_case = L"def f(a, b=2):\n"
                               "    return a + b\n"
                               "f(1)\n";

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 2\n"
                           "    2 Star0\n"
                           "    3 CreateTuple {r0:1}\n"
                           "    6 Star1\n"
                           "    7 CreateFunctionWithDefaults c[0], r1\n"
                           "   10 StaGlobal [0]\n"
                           "   15 LdaGlobal [0]\n"
                           "   20 Star0\n"
                           "   21 LdaSmi 1\n"
                           "   23 Star a0\n"
                           "   25 CallSimple r0, {a0:1}, call_ic[0]\n"
                           "   30 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p1\n"
                           "    2 Add p0\n"
                           "    4 Return\n"
                           "    5 LdaNone\n"
                           "    6 Return\n"
                           "\n";
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
        module_code->constant_table[0].as_value().get_ptr<CodeObject>();

    EXPECT_EQ(3, function_code->n_parameters);
    EXPECT_EQ(2, function_code->n_positional_parameters);
    EXPECT_TRUE(function_code->has_varargs());
    EXPECT_EQ(7, function_code->get_highest_occupied_frame_offset());
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
                     .as_value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(5, two_params->constant_table[0]
                     .as_value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(7, three_params->constant_table[0]
                     .as_value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(7, four_params->constant_table[0]
                     .as_value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
}

TEST(Codegen, binary_expression_reuses_local_register_operand)
{
    const wchar_t *test_case = L"def add(a, b):\n"
                               "    return a + b\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p1\n"
                           "    2 Add p0\n"
                           "    4 Return\n"
                           "    5 LdaNone\n"
                           "    6 Return\n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, comparison_reuses_local_register_operand)
{
    const wchar_t *test_case = L"def lt(a, b):\n"
                               "    return a < b\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p1\n"
                           "    2 TestLess p0\n"
                           "    4 Return\n"
                           "    5 LdaNone\n"
                           "    6 Return\n"
                           "\n";
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
                           "Constant 0: \n";
    std::string actual = bytecode_str_from_file(L"assert False, \"lol\"\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, raise_statement_evaluates_expression_and_unwinds)
{
    std::string expected = "Code object:\n"
                           "    0 LdaGlobal [0]\n"
                           "    5 RaiseUnwind\n"
                           "    6 Return\n";
    std::string actual = bytecode_str_from_file(L"raise ValueError\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, try_bare_except_emits_exception_table)
{
    std::string expected = "Code object:\n"
                           "    0 LdaGlobal [0]\n"
                           "    5 RaiseUnwind\n"
                           "    6 Jump 20\n"
                           "    9 ClearActiveException\n"
                           "   10 LdaSmi 7\n"
                           "   12 StaGlobal [1]\n"
                           "   17 Jump 20\n"
                           "   20 LdaGlobal [1]\n"
                           "   25 Return\n"
                           "Exception table:\n"
                           "    0..6 -> 9\n";
    std::string actual = bytecode_str_from_file(L"try:\n"
                                                L"    raise ValueError\n"
                                                L"except:\n"
                                                L"    result = 7\n"
                                                L"result\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, try_typed_except_checks_active_exception)
{
    std::string expected = "Code object:\n"
                           "    0 LdaGlobal [0]\n"
                           "    5 RaiseUnwind\n"
                           "    6 Jump 30\n"
                           "    9 LdaGlobal [1]\n"
                           "   14 ActiveExceptionIsInstance\n"
                           "   15 JumpIfFalse 29\n"
                           "   18 ClearActiveException\n"
                           "   19 LdaSmi 7\n"
                           "   21 StaGlobal [2]\n"
                           "   26 Jump 30\n"
                           "   29 ReraiseActiveException\n"
                           "   30 LdaGlobal [2]\n"
                           "   35 Return\n"
                           "Exception table:\n"
                           "    0..6 -> 9\n";
    std::string actual = bytecode_str_from_file(L"try:\n"
                                                L"    raise ValueError\n"
                                                L"except Exception:\n"
                                                L"    result = 7\n"
                                                L"result\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, startup_wrapper_uses_exception_table_for_unhandled_exception)
{
    test::VmTestContext test_context;
    ThreadState::ActivationScope activation_scope(test_context.thread());
    CodeObject *entry_code_object =
        test_context.compile_file(L"raise ValueError\n");
    TValue<CodeObject> wrapper =
        make_startup_wrapper_code_object(entry_code_object);

    std::string actual = fmt::to_string(*wrapper.extract());
    std::string expected = "Code object:\n"
                           "    0 CallCodeObject c[0], a0, 0\n"
                           "    4 Halt\n"
                           "    5 RaiseIfUnhandledException\n"
                           "    6 Halt\n"
                           "Exception table:\n"
                           "    0..4 -> 5\n"
                           "Constant 0: Code object:\n"
                           "    0 LdaGlobal [0]\n"
                           "    5 RaiseUnwind\n"
                           "    6 Return\n"
                           "\n";
    EXPECT_EQ(expected, actual);
}

TEST(Codegen, function_implicit_return_none)
{
    const wchar_t *test_case = L"def f():\n"
                               "    a = 1\n"
                               "f()\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaGlobal [0]\n"
                           "   12 Star0\n"
                           "   13 CallSimple r0, {a0:0}, call_ic[0]\n"
                           "   18 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 Star0\n"
                           "    3 LdaNone\n"
                           "    4 Return\n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, class_definition_passes_hidden_name_and_bases_to_create_class)
{
    const wchar_t *test_case = L"class Cls:\n"
                               "    pass\n"
                               "Cls\n";

    std::string expected = "Code object:\n"
                           "    0 LdaConstant c[1]\n"
                           "    2 Star a0\n"
                           "    4 LdaConstant c[2]\n"
                           "    6 Star0\n"
                           "    7 CreateTuple {r0:1}\n"
                           "   10 Star a1\n"
                           "   12 CreateClass c[0], a0\n"
                           "   15 StaGlobal [0]\n"
                           "   20 LdaGlobal [0]\n"
                           "   25 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 BuildClass\n"
                           "\n"
                           "Constant 1: \n"
                           "Constant 2: \n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, string_literal_constant_value)
{
    test::VmTestContext test_context;
    CodeObject *code_obj = test_context.compile_file(L"\"abc\"\n");

    ASSERT_EQ(size_t(1), code_obj->constant_table.size());
    EXPECT_STREQ(L"abc", string_as_wchar_t(TValue<String>::from_value_checked(
                             code_obj->constant_table[0].as_value())));
}

TEST(Codegen, attribute_load_uses_register_receiver)
{
    const wchar_t *test_case = L"def get(obj):\n"
                               "    return obj.value\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 LoadAttr p0, c[0], read_ic[0]\n"
                           "    4 Return\n"
                           "    5 LdaNone\n"
                           "    6 Return\n"
                           "Constant 0: \n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, attribute_store_uses_register_receiver_and_accumulator_value)
{
    const wchar_t *test_case = L"def set(obj, value):\n"
                               "    obj.value = value\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p1\n"
                           "    2 StoreAttr p0, c[0], mutation_ic[0]\n"
                           "    6 LdaNone\n"
                           "    7 Return\n"
                           "Constant 0: \n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, attribute_delete_uses_register_receiver_and_mutation_cache)
{
    const wchar_t *test_case = L"def clear(obj):\n"
                               "    del obj.value\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 DelAttr p0, c[0], mutation_ic[0]\n"
                           "    4 LdaNone\n"
                           "    5 Return\n"
                           "Constant 0: \n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, global_variable_delete_uses_binding_slot)
{
    const wchar_t *test_case = L"value = 1\n"
                               "del value\n";

    std::string expected = "Code object:\n"
                           "    0 LdaSmi 1\n"
                           "    2 StaGlobal [0]\n"
                           "    7 DelGlobal [0]\n"
                           "   12 Return\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, direct_method_call_uses_callmethodattr)
{
    const wchar_t *test_case = L"def invoke(obj, value):\n"
                               "    return obj.method(value)\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p0\n"
                           "    2 Star a0\n"
                           "    4 Ldar p1\n"
                           "    6 Star a1\n"
                           "    8 CallMethodAttr a0, c[0], read_ic[0], "
                           "call_ic[0], 1\n"
                           "   14 Return\n"
                           "   15 LdaNone\n"
                           "   16 Return\n"
                           "Constant 0: \n"
                           "\n";
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

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p1\n"
                           "    2 LoadSubscript p0\n"
                           "    4 Return\n"
                           "    5 LdaNone\n"
                           "    6 Return\n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, subscript_store_uses_receiver_and_key_registers)
{
    const wchar_t *test_case = L"def set(obj, idx, value):\n"
                               L"    obj[idx] = value\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p2\n"
                           "    2 StoreSubscript p0, p1\n"
                           "    5 LdaNone\n"
                           "    6 Return\n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, subscript_delete_uses_receiver_and_key_registers)
{
    const wchar_t *test_case = L"def clear(obj, idx):\n"
                               L"    del obj[idx]\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 DelSubscript p0, p1\n"
                           "    3 LdaNone\n"
                           "    4 Return\n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, subscript_augmented_assignment_evaluates_receiver_and_key_once)
{
    const wchar_t *test_case = L"def bump(obj, idx):\n"
                               L"    obj[idx] += 1\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Return\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p1\n"
                           "    2 LoadSubscript p0\n"
                           "    4 AddSmi 1\n"
                           "    6 StoreSubscript p0, p1\n"
                           "    9 LdaNone\n"
                           "   10 Return\n"
                           "\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, direct_range_for_loop_uses_specialized_fast_path_with_fallback)
{
    std::string expected = "Code object:\n"
                           "    0 LdaSmi 0\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaGlobal [1]\n"
                           "   12 Star0\n"
                           "   13 LdaSmi 3\n"
                           "   15 Star1\n"
                           "   16 ForPrepRange1 r0, 50\n"
                           "   20 ForIterRange1 r0, 90\n"
                           "   24 StaGlobal [2]\n"
                           "   29 LdaGlobal [0]\n"
                           "   34 Star3\n"
                           "   35 LdaGlobal [2]\n"
                           "   40 Add r3\n"
                           "   42 StaGlobal [0]\n"
                           "   47 Jump 20\n"
                           "   50 Ldar1\n"
                           "   51 Star a0\n"
                           "   53 CallSimple r0, {a0:1}, call_ic[0]\n"
                           "   58 GetIter\n"
                           "   59 Star2\n"
                           "   60 ForIter r2, 90\n"
                           "   64 StaGlobal [2]\n"
                           "   69 LdaGlobal [0]\n"
                           "   74 Star3\n"
                           "   75 LdaGlobal [2]\n"
                           "   80 Add r3\n"
                           "   82 StaGlobal [0]\n"
                           "   87 Jump 60\n"
                           "   90 LdaGlobal [0]\n"
                           "   95 Return\n";
    std::string actual = bytecode_str_from_file(L"total = 0\n"
                                                "for x in range(3):\n"
                                                "    total += x\n"
                                                "total\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, non_direct_for_loop_still_uses_generic_iterator_bytecodes)
{
    std::string expected = "Code object:\n"
                           "    0 LdaGlobal [0]\n"
                           "    5 Star0\n"
                           "    6 LdaSmi 3\n"
                           "    8 Star a0\n"
                           "   10 CallSimple r0, {a0:1}, call_ic[0]\n"
                           "   15 StaGlobal [1]\n"
                           "   20 LdaGlobal [1]\n"
                           "   25 GetIter\n"
                           "   26 Star0\n"
                           "   27 ForIter r0, 44\n"
                           "   31 StaGlobal [2]\n"
                           "   36 LdaGlobal [2]\n"
                           "   41 Jump 27\n"
                           "   44 Return\n";
    std::string actual = bytecode_str_from_file(L"it = range(3)\n"
                                                "for x in it:\n"
                                                "    x\n");

    EXPECT_EQ(expected, actual);
}
