#include "bytecode/bytecode.h"
#include "bytecode/bytecode_instruction.h"
#include "bytecode/code_object.h"
#include "test_helpers.h"
#include <gtest/gtest.h>
#include <string_view>

using namespace cl;

namespace
{
    BytecodeInstruction find_instruction(const CodeObject &code_object,
                                         Bytecode encoded_opcode)
    {
        for(uint32_t pc_offset = 0; pc_offset < code_object.size();)
        {
            BytecodeInstruction instruction =
                decode_instruction(code_object, pc_offset);
            if(instruction.encoded_opcode() == encoded_opcode)
            {
                return instruction;
            }
            pc_offset = instruction.next_pc_offset();
        }
        ADD_FAILURE() << "instruction not found: "
                      << bytecode_name(encoded_opcode);
        return {};
    }
}  // namespace

TEST(BytecodeFormat, every_opcode_has_authoritative_metadata)
{
    for(size_t idx = 0; idx < size_t(Bytecode::Invalid); ++idx)
    {
        Bytecode opcode = Bytecode(idx);
        const BytecodeInfo &info = bytecode_info(opcode);
        EXPECT_TRUE(is_valid_bytecode(opcode));
        EXPECT_FALSE(std::string_view(info.name).empty());
        EXPECT_NE(BytecodeFormat::Invalid, info.format);
        EXPECT_NE(BytecodeControlFlow::Invalid, info.control_flow);
        EXPECT_GT(info.length(), 0);
    }

    const BytecodeInfo &invalid = bytecode_info(Bytecode::Invalid);
    EXPECT_FALSE(is_valid_bytecode(Bytecode::Invalid));
    EXPECT_EQ("Invalid", std::string_view(invalid.name));
    EXPECT_EQ(BytecodeFormat::Invalid, invalid.format);
    EXPECT_EQ(BytecodeControlFlow::Invalid, invalid.control_flow);
    EXPECT_EQ(0, invalid.length());
}

TEST(BytecodeFormat, representative_operand_formats_have_expected_lengths)
{
    EXPECT_EQ(1, bytecode_length(Bytecode::LdaNone));
    EXPECT_EQ(2, bytecode_length(Bytecode::LdaConstant));
    EXPECT_EQ(3, bytecode_length(Bytecode::Mov));
    EXPECT_EQ(4, bytecode_length(Bytecode::LoadAttr));
    EXPECT_EQ(6, bytecode_length(Bytecode::CallMethodAttrPositional));
    EXPECT_EQ(9, bytecode_length(Bytecode::CallMethodAttrKeyword));
    EXPECT_EQ(8, bytecode_length(Bytecode::CallKeyword));
    EXPECT_EQ(7, bytecode_length(Bytecode::DictInsertNew));
}

TEST(BytecodeFormat, control_flow_metadata_identifies_jump_operands)
{
    struct TestCase
    {
        Bytecode opcode;
        BytecodeControlFlow control_flow;
        int8_t jump_offset;
    };

    constexpr TestCase test_cases[] = {
        {Bytecode::LdaNone, BytecodeControlFlow::Fallthrough, -1},
        {Bytecode::Jump, BytecodeControlFlow::UnconditionalJump, 1},
        {Bytecode::JumpIfTrue, BytecodeControlFlow::ConditionalJump, 1},
        {Bytecode::JumpIfEqualSmi, BytecodeControlFlow::ConditionalJump, 2},
        {Bytecode::ForIter, BytecodeControlFlow::ConditionalJump, 2},
        {Bytecode::BuildClass, BytecodeControlFlow::Terminator, -1},
        {Bytecode::Return, BytecodeControlFlow::Terminator, -1},
        {Bytecode::RaiseUnwind, BytecodeControlFlow::Terminator, -1},
    };

    for(const TestCase &test_case: test_cases)
    {
        const BytecodeInfo &info = bytecode_info(test_case.opcode);
        EXPECT_EQ(test_case.control_flow, info.control_flow);
        EXPECT_EQ(test_case.jump_offset, info.relative_jump_operand_offset());
    }
}

TEST(BytecodeFormat, operator_continuations_have_explicit_roles)
{
    EXPECT_EQ(BytecodeCompoundRole::BinaryOperator,
              bytecode_info(Bytecode::Add).compound_role);
    EXPECT_EQ(BytecodeCompoundRole::TernaryOperator,
              bytecode_info(Bytecode::TernaryPow).compound_role);
    EXPECT_EQ(
        BytecodeCompoundRole::BinaryOperatorContinuation,
        bytecode_info(Bytecode::CheckOperatorNotImplemented).compound_role);
    EXPECT_EQ(BytecodeCompoundRole::TernaryOperatorContinuation,
              bytecode_info(Bytecode::CheckTernaryOperatorNotImplemented)
                  .compound_role);
    EXPECT_EQ(BytecodeCompoundRole::None,
              bytecode_info(Bytecode::Contains).compound_role);
}

TEST(BytecodeFormat, compact_register_opcodes_remain_contiguous)
{
    EXPECT_EQ(n_fastpath_ldar_star,
              int32_t(Bytecode::Ldar15) - int32_t(Bytecode::Ldar0) + 1);
    EXPECT_EQ(n_fastpath_ldar_star,
              int32_t(Bytecode::Star15) - int32_t(Bytecode::Star0) + 1);
    EXPECT_EQ(int32_t(Bytecode::Ldar15) + 1, int32_t(Bytecode::Star0));
}

TEST(BytecodeInstruction, compact_register_forms_are_normalized)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"def f(x): return x\n"
                                                   L"f(1)\n");

    BytecodeInstruction instruction =
        find_instruction(*code_object, Bytecode::Star0);
    EXPECT_EQ(Bytecode::Star0, instruction.encoded_opcode());
    EXPECT_EQ(Bytecode::Star, instruction.semantic_opcode());
    ASSERT_EQ(1, instruction.operands().size());
    EXPECT_EQ(BytecodeOperandKind::Register, instruction.operands()[0].kind);
    ASSERT_EQ(1, instruction.sources().size());
    EXPECT_EQ(BytecodeValueLocationKind::Accumulator,
              instruction.sources()[0].kind);
    ASSERT_EQ(1, instruction.destinations().size());
    EXPECT_EQ(BytecodeValueLocationKind::Temporary,
              instruction.destinations()[0].kind);
}

TEST(BytecodeInstruction, compound_operator_exposes_continuation)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"1 << 4\n");

    BytecodeInstruction instruction =
        find_instruction(*code_object, Bytecode::LShiftSmi);
    ASSERT_TRUE(instruction.continuation_pc_offset().has_value());
    EXPECT_EQ(instruction.pc_offset() + bytecode_length(Bytecode::LShiftSmi),
              *instruction.continuation_pc_offset());
    EXPECT_EQ(*instruction.continuation_pc_offset() +
                  bytecode_length(Bytecode::CheckOperatorNotImplemented),
              instruction.next_pc_offset());
    ASSERT_TRUE(instruction.cache().has_value());
    EXPECT_EQ(InlineCacheKind::Operator, instruction.cache()->kind);
    EXPECT_FALSE(instruction.cache2().has_value());
    ASSERT_EQ(1, instruction.sources().size());
    ASSERT_EQ(1, instruction.destinations().size());
    EXPECT_EQ(BytecodeValueLocationKind::Accumulator,
              instruction.sources()[0].kind);
    EXPECT_EQ(BytecodeValueLocationKind::Accumulator,
              instruction.destinations()[0].kind);

    BytecodeInstruction continuation =
        decode_instruction(*code_object, *instruction.continuation_pc_offset());
    EXPECT_EQ(Bytecode::CheckOperatorNotImplemented,
              continuation.encoded_opcode());
    EXPECT_FALSE(continuation.continuation_pc_offset().has_value());
}

TEST(BytecodeInstruction, two_cache_instruction_uses_cache_and_cache2)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"x.bit_length()\n");

    BytecodeInstruction instruction =
        find_instruction(*code_object, Bytecode::CallMethodAttrPositional);
    ASSERT_TRUE(instruction.cache().has_value());
    ASSERT_TRUE(instruction.cache2().has_value());
    EXPECT_EQ(InlineCacheKind::AttributeRead, instruction.cache()->kind);
    EXPECT_EQ(InlineCacheKind::FunctionCall, instruction.cache2()->kind);
    ASSERT_EQ(1, instruction.sources().size());
    EXPECT_EQ(BytecodeValueLocationKind::Temporary,
              instruction.sources()[0].kind);
    ASSERT_EQ(1, instruction.destinations().size());
    EXPECT_EQ(BytecodeValueLocationKind::Accumulator,
              instruction.destinations()[0].kind);
}

TEST(BytecodeInstruction, range_loop_effects_are_uniform_and_rmw)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"for x in range(5):\n"
                                                   L"    pass\n");

    BytecodeInstruction prep =
        find_instruction(*code_object, Bytecode::ForPrepRange1);
    ASSERT_EQ(2, prep.operands().size());
    EXPECT_EQ(BytecodeOperandKind::RelativeJumpI16, prep.operands()[1].kind);
    EXPECT_EQ(10, prep.operands()[1].signed_value());
    ASSERT_TRUE(prep.jump_target_pc_offset().has_value());
    EXPECT_EQ(21, *prep.jump_target_pc_offset());
    ASSERT_EQ(2, prep.sources().size());
    ASSERT_EQ(2, prep.destinations().size());
    EXPECT_EQ(prep.sources()[0].register_index,
              prep.destinations()[0].register_index);
    EXPECT_EQ(prep.sources()[1].register_index,
              prep.destinations()[1].register_index);

    BytecodeInstruction iter =
        find_instruction(*code_object, Bytecode::ForIterRange1);
    ASSERT_EQ(2, iter.sources().size());
    ASSERT_EQ(2, iter.destinations().size());
    EXPECT_EQ(BytecodeValueLocationKind::Temporary,
              iter.destinations()[0].kind);
    EXPECT_EQ(BytecodeValueLocationKind::Accumulator,
              iter.destinations()[1].kind);

    BytecodeInstruction backedge =
        find_instruction(*code_object, Bytecode::Jump);
    ASSERT_TRUE(backedge.jump_target_pc_offset().has_value());
    EXPECT_LT(backedge.operands()[0].signed_value(), 0);
    EXPECT_EQ(11, *backedge.jump_target_pc_offset());
}
