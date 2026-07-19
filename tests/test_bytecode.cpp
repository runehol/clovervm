#include "bytecode/bytecode.h"
#include "bytecode/bytecode_decoder.h"
#include "bytecode/bytecode_instruction.h"
#include "bytecode/code_object.h"
#include "test_helpers.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <string_view>
#include <vector>

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

    const BytecodeBlock &find_block(const BytecodeDecoder &decoder,
                                    uint32_t start_pc_offset)
    {
        for(const BytecodeBlock &block: decoder.blocks())
        {
            if(block.start_pc_offset() == start_pc_offset)
            {
                return block;
            }
        }
        ADD_FAILURE() << "block not found at offset " << start_pc_offset;
        return decoder.blocks().front();
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
    EXPECT_EQ(nullptr, instruction.operator_cache());
    EXPECT_NE(instruction.operands().end(),
              std::find_if(
                  instruction.operands().begin(), instruction.operands().end(),
                  [](const BytecodeOperand &operand) {
                      return operand.kind == BytecodeOperandKind::OperatorCache;
                  }));
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

TEST(BytecodeInstruction, two_cache_instruction_preserves_both_typed_operands)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"x.bit_length()\n");

    BytecodeInstruction instruction =
        find_instruction(*code_object, Bytecode::CallMethodAttrPositional);
    EXPECT_EQ(nullptr, instruction.attribute_read_cache());
    EXPECT_EQ(nullptr, instruction.function_call_cache());
    bool has_attribute_read_cache = false;
    bool has_function_call_cache = false;
    for(const BytecodeOperand &operand: instruction.operands())
    {
        has_attribute_read_cache |=
            operand.kind == BytecodeOperandKind::AttributeReadCache;
        has_function_call_cache |=
            operand.kind == BytecodeOperandKind::FunctionCallCache;
    }
    EXPECT_TRUE(has_attribute_read_cache);
    EXPECT_TRUE(has_function_call_cache);
    ASSERT_EQ(1, instruction.sources().size());
    EXPECT_EQ(BytecodeValueLocationKind::Temporary,
              instruction.sources()[0].kind);
    ASSERT_EQ(1, instruction.destinations().size());
    EXPECT_EQ(BytecodeValueLocationKind::Accumulator,
              instruction.destinations()[0].kind);

    BytecodeDecoder decoder(*code_object);
    bool found_decoded_call = false;
    for(const BytecodeBlock &block: decoder.blocks())
    {
        for(BytecodeInstruction decoded: block.instructions())
        {
            if(decoded.encoded_opcode() == Bytecode::CallMethodAttrPositional)
            {
                found_decoded_call = true;
                EXPECT_NE(nullptr, decoded.attribute_read_cache());
                EXPECT_NE(nullptr, decoded.function_call_cache());
                EXPECT_EQ(nullptr, decoded.keyword_call_cache());
                EXPECT_EQ(nullptr, decoded.operator_cache());
            }
        }
    }
    EXPECT_TRUE(found_decoded_call);
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

TEST(KeywordCallInlineCache, copy_clones_keyword_destination_registers)
{
    InlineCacheTables live_tables;
    live_tables.keyword_call_caches.emplace_back();
    KeywordCallInlineCache &live = live_tables.keyword_call_caches[0];
    live.guard_value = Value::from_smi(42);
    live.n_kw_args = 3;
    live.keyword_dest_regs = std::make_unique<int8_t[]>(live.n_kw_args);
    live.keyword_dest_regs[0] = -1;
    live.keyword_dest_regs[1] = -2;
    live.keyword_dest_regs[2] = -3;

    InlineCacheTables snapshot_tables = live_tables;
    ASSERT_EQ(1, snapshot_tables.keyword_call_caches.size());
    const KeywordCallInlineCache &snapshot =
        snapshot_tables.keyword_call_caches[0];
    EXPECT_EQ(live.guard_value, snapshot.guard_value);
    EXPECT_EQ(live.n_kw_args, snapshot.n_kw_args);
    ASSERT_NE(nullptr, snapshot.keyword_dest_regs);
    EXPECT_NE(live.keyword_dest_regs.get(), snapshot.keyword_dest_regs.get());
    EXPECT_EQ(-1, snapshot.keyword_dest_regs[0]);
    EXPECT_EQ(-2, snapshot.keyword_dest_regs[1]);
    EXPECT_EQ(-3, snapshot.keyword_dest_regs[2]);

    live.keyword_dest_regs[1] = -4;
    EXPECT_EQ(-2, snapshot.keyword_dest_regs[1]);
}

TEST(BytecodeDecoder, builds_normal_control_flow_and_exception_entrances)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"try:\n"
                                                   L"    raise ValueError\n"
                                                   L"except:\n"
                                                   L"    result = 7\n"
                                                   L"result\n");

    BytecodeDecoder decoder(*code_object);
    ASSERT_EQ(4, decoder.blocks().size());

    const BytecodeBlock &protected_block = find_block(decoder, 0);
    EXPECT_EQ(4, protected_block.end_pc_offset());
    EXPECT_TRUE(protected_block.successors().empty());
    ASSERT_TRUE(protected_block.exception_handler_index().has_value());
    EXPECT_EQ(0, *protected_block.exception_handler_index());

    const BytecodeBlock &jump_over_handler = find_block(decoder, 4);
    ASSERT_EQ(1, jump_over_handler.successors().size());
    EXPECT_EQ(find_block(decoder, 16).id(), jump_over_handler.successors()[0]);

    const BytecodeBlock &handler = find_block(decoder, 7);
    ASSERT_EQ(1, handler.exception_entrances().size());
    EXPECT_EQ(0, handler.exception_entrances()[0]);
    ASSERT_EQ(1, handler.successors().size());
    EXPECT_EQ(find_block(decoder, 16).id(), handler.successors()[0]);

    const BytecodeBlock &join = find_block(decoder, 16);
    EXPECT_EQ(2, join.predecessors().size());
}

TEST(BytecodeDecoder, records_conditional_edges_and_loop_backedges)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"for x in range(5):\n"
                                                   L"    pass\n");

    BytecodeDecoder decoder(*code_object);
    bool found_conditional = false;
    bool found_backedge = false;
    for(const BytecodeBlock &block: decoder.blocks())
    {
        found_conditional |= block.successors().size() == 2;
        for(BytecodeBlockId successor: block.successors())
        {
            found_backedge |= successor <= block.id();
            const std::vector<BytecodeBlockId> &predecessors =
                decoder.blocks()[successor].predecessors();
            EXPECT_NE(predecessors.end(),
                      std::find(predecessors.begin(), predecessors.end(),
                                block.id()));
        }
    }

    EXPECT_TRUE(found_conditional);
    EXPECT_TRUE(found_backedge);
}

TEST(BytecodeDecoder, block_iteration_yields_compound_operator_once)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"1 << 4\n");

    BytecodeDecoder decoder(*code_object);
    uint32_t instruction_count = 0;
    uint32_t operator_count = 0;
    for(const BytecodeBlock &block: decoder.blocks())
    {
        uint32_t final_next_pc_offset = block.start_pc_offset();
        for(BytecodeInstruction instruction: block.instructions())
        {
            ++instruction_count;
            final_next_pc_offset = instruction.next_pc_offset();
            if(instruction.encoded_opcode() == Bytecode::LShiftSmi)
            {
                ++operator_count;
                ASSERT_TRUE(instruction.continuation_pc_offset().has_value());
                EXPECT_NE(nullptr, instruction.operator_cache());
                EXPECT_EQ(nullptr, instruction.function_call_cache());
            }
            EXPECT_NE(Bytecode::CheckOperatorNotImplemented,
                      instruction.encoded_opcode());
        }
        EXPECT_EQ(block.end_pc_offset(), final_next_pc_offset);
    }

    EXPECT_GT(instruction_count, 0);
    EXPECT_EQ(1, operator_count);
}

TEST(BytecodeDecoder, block_instructions_use_stable_cache_snapshots)
{
    test::VmTestContext context;
    CodeObject *code_object = context.compile_file(L"f(1)\n");
    ASSERT_EQ(1, code_object->inline_caches.function_call_caches.size());
    FunctionCallInlineCache &live_cache =
        code_object->inline_caches.function_call_caches[0];
    live_cache.n_args = 12;

    BytecodeInstruction standalone =
        find_instruction(*code_object, Bytecode::CallPositional);
    EXPECT_EQ(nullptr, standalone.function_call_cache());

    BytecodeDecoder decoder(*code_object);
    live_cache.n_args = 37;

    const FunctionCallInlineCache *snapshot = nullptr;
    for(const BytecodeBlock &block: decoder.blocks())
    {
        for(BytecodeInstruction instruction: block.instructions())
        {
            if(instruction.encoded_opcode() == Bytecode::CallPositional)
            {
                snapshot = instruction.function_call_cache();
            }
        }
    }

    ASSERT_NE(nullptr, snapshot);
    EXPECT_NE(&live_cache, snapshot);
    EXPECT_EQ(12, snapshot->n_args);
}
