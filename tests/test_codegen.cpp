#include "code_object.h"
#include "code_object_print.h"
#include "parser.h"
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
                           "    6 Halt\n";
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
                           "   19 Halt\n";
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
                           "   48 Halt\n";
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
                           "   54 Halt\n";
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
                           "   12 Star a0\n"
                           "   14 LdaSmi 1\n"
                           "   16 Star a1\n"
                           "   18 LdaSmi 2\n"
                           "   20 Star a2\n"
                           "   22 LdaSmi 3\n"
                           "   24 Star a3\n"
                           "   26 CallSimple a0, a1a3\n"
                           "   29 Halt\n"
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

    EXPECT_EQ(3, one_param->constant_table[0]
                     .as_value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(3, two_params->constant_table[0]
                     .as_value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(5, three_params->constant_table[0]
                     .as_value()
                     .get_ptr<CodeObject>()
                     ->get_highest_occupied_frame_offset());
    EXPECT_EQ(5, four_params->constant_table[0]
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
                           "    7 Halt\n"
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
                           "    7 Halt\n"
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

TEST(Codegen, function_implicit_return_none)
{
    const wchar_t *test_case = L"def f():\n"
                               "    a = 1\n"
                               "f()\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 LdaGlobal [0]\n"
                           "   12 Star a0\n"
                           "   14 CallSimple a0\n"
                           "   17 Halt\n"
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
                           "    2 Star0\n"
                           "    3 LdaConstant c[2]\n"
                           "    5 Star2\n"
                           "    6 CreateTuple r2, 1\n"
                           "    9 Star1\n"
                           "   10 CreateClass c[0], r0\n"
                           "   13 StaGlobal [0]\n"
                           "   18 LdaGlobal [0]\n"
                           "   23 Halt\n"
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
    EXPECT_STREQ(L"abc", string_as_wchar_t(TValue<String>(
                             code_obj->constant_table[0].as_value())));
}

TEST(Codegen, attribute_load_uses_register_receiver)
{
    const wchar_t *test_case = L"def get(obj):\n"
                               "    return obj.value\n";

    std::string expected = "Code object:\n"
                           "    0 CreateFunction c[0]\n"
                           "    2 StaGlobal [0]\n"
                           "    7 Halt\n"
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
                           "    7 Halt\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p1\n"
                           "    2 StoreAttr p0, c[0], write_ic[0]\n"
                           "    6 LdaNone\n"
                           "    7 Return\n"
                           "Constant 0: \n"
                           "\n";
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
                           "    7 Halt\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p0\n"
                           "    2 Star a0\n"
                           "    4 Ldar p1\n"
                           "    6 Star a1\n"
                           "    8 CallMethodAttr a0, c[0], read_ic[0], 1\n"
                           "   13 Return\n"
                           "   14 LdaNone\n"
                           "   15 Return\n"
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
                           "    9 CreateList r0, 3\n"
                           "   12 Halt\n";
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
                           "    9 CreateTuple r0, 3\n"
                           "   12 Halt\n";
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
                           "    9 CreateTuple r0, 3\n"
                           "   12 Halt\n";
    std::string actual = bytecode_str_from_file(test_case);

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, empty_tuple_literal_uses_createtuple)
{
    const wchar_t *test_case = L"()\n";

    std::string expected = "Code object:\n"
                           "    0 CreateTuple r0, 0\n"
                           "    3 Halt\n";
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
                           "   12 CreateDict r0, 2\n"
                           "   15 Halt\n";
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
                           "    7 Halt\n"
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
                           "    7 Halt\n"
                           "Constant 0: Code object:\n"
                           "    0 Ldar p2\n"
                           "    2 StoreSubscript p0, p1\n"
                           "    5 LdaNone\n"
                           "    6 Return\n"
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
                           "    7 Halt\n"
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
                           "    7 LdaGlobal [2]\n"
                           "   12 Star0\n"
                           "   13 LdaSmi 3\n"
                           "   15 Star1\n"
                           "   16 ForPrepRange1 r0, 50\n"
                           "   20 ForIterRange1 r0, 85\n"
                           "   24 StaGlobal [1]\n"
                           "   29 LdaGlobal [0]\n"
                           "   34 Star3\n"
                           "   35 LdaGlobal [1]\n"
                           "   40 Add r3\n"
                           "   42 StaGlobal [0]\n"
                           "   47 Jump 20\n"
                           "   50 CallSimple r0, r1\n"
                           "   53 GetIter\n"
                           "   54 Star2\n"
                           "   55 ForIter r2, 85\n"
                           "   59 StaGlobal [1]\n"
                           "   64 LdaGlobal [0]\n"
                           "   69 Star3\n"
                           "   70 LdaGlobal [1]\n"
                           "   75 Add r3\n"
                           "   77 StaGlobal [0]\n"
                           "   82 Jump 55\n"
                           "   85 LdaGlobal [0]\n"
                           "   90 Halt\n";
    std::string actual = bytecode_str_from_file(L"total = 0\n"
                                                "for x in range(3):\n"
                                                "    total += x\n"
                                                "total\n");

    EXPECT_EQ(expected, actual);
}

TEST(Codegen, non_direct_for_loop_still_uses_generic_iterator_bytecodes)
{
    std::string expected = "Code object:\n"
                           "    0 LdaGlobal [1]\n"
                           "    5 Star a0\n"
                           "    7 LdaSmi 3\n"
                           "    9 Star a1\n"
                           "   11 CallSimple a0, a1\n"
                           "   14 StaGlobal [0]\n"
                           "   19 LdaGlobal [0]\n"
                           "   24 GetIter\n"
                           "   25 Star0\n"
                           "   26 ForIter r0, 43\n"
                           "   30 StaGlobal [2]\n"
                           "   35 LdaGlobal [2]\n"
                           "   40 Jump 26\n"
                           "   43 Halt\n";
    std::string actual = bytecode_str_from_file(L"it = range(3)\n"
                                                "for x in it:\n"
                                                "    x\n");

    EXPECT_EQ(expected, actual);
}
