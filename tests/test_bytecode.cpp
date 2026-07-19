#include "bytecode/bytecode.h"
#include <gtest/gtest.h>
#include <string_view>

using namespace cl;

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
