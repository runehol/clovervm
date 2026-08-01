#include "jit/core_bytecode_translator.h"

#include "runtime/fatal.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cl::jit
{
    ControlFlowGraph *CoreBytecodeTranslator::translate()
    {
        const std::vector<BytecodeBlock> &bytecode_blocks = decoder_.blocks();
        assert(!bytecode_blocks.empty());
        assert(
            bytecode_blocks[decoder_.entry_block_id()].predecessors().empty());

        builder_.set_bytecode_state_order(state_tracker_.order());
        builder_.emplace_n_blocks(bytecode_blocks.size());
        for(const BytecodeBlock &bytecode_block: bytecode_blocks)
        {
            translate_block(bytecode_block);
        }
        return builder_.finalize();
    }

    CoreBytecodeTranslator::State
    CoreBytecodeTranslator::make_entry_state(Block *block)
    {
        std::vector<ProgramValueRef> parameters;
        parameters.reserve(state_tracker_.n_parameters());
        for(uint32_t index = 0; index < state_tracker_.n_parameters(); ++index)
        {
            (void)index;
            parameters.emplace_back(
                builder_.emplace_parameter<ParameterInstruction>(block));
        }

        std::vector<ProgramValueRef> frame_header;
        frame_header.reserve(FrameHeaderSize);
        for(int32_t frame_offset = FrameHeaderPreviousFpOffset;
            frame_offset <= FrameHeaderReturnPcOffset; ++frame_offset)
        {
            frame_header.push_back(emplace_state_parameter(
                block, state_tracker_.order().position_for_frame_offset(
                           frame_offset)));
        }

        ProgramValueRef uninitialized_local(
            builder_.emplace_instruction<UninitializedInstruction>(block));
        ProgramValueRef uninitialized_temporary(
            builder_.emplace_instruction<UninitializedInstruction>(block));
        return state_tracker_.make_entry_state(parameters, frame_header,
                                               uninitialized_local,
                                               uninitialized_temporary);
    }

    CoreBytecodeTranslator::State
    CoreBytecodeTranslator::make_block_entry_state(Block *block)
    {
        std::vector<ProgramValueRef> parameters;
        parameters.reserve(state_tracker_.block_parameter_count());
        for(size_t index = 0; index < state_tracker_.block_parameter_count();
            ++index)
        {
            parameters.push_back(emplace_state_parameter(block, index));
        }
        return state_tracker_.make_state_from_block_parameters(parameters);
    }

    ProgramValueRef
    CoreBytecodeTranslator::emplace_state_parameter(Block *block,
                                                    size_t state_position)
    {
        const BytecodeStateOrder &order = state_tracker_.order();
        if(state_position >= BytecodeStateOrder::FirstFramePosition)
        {
            int32_t frame_offset = order.frame_offset_at(state_position);
            if(frame_offset >= FrameHeaderPreviousFpOffset &&
               frame_offset <= FrameHeaderReturnPcOffset &&
               frame_header_value_is_pointer(frame_offset))
            {
                return ProgramValueRef(
                    builder_.emplace_parameter<ParameterPointerInstruction>(
                        block));
            }
        }
        return ProgramValueRef(
            builder_.emplace_parameter<ParameterInstruction>(block));
    }

    void
    CoreBytecodeTranslator::translate_block(const BytecodeBlock &bytecode_block)
    {
        Block *block = builder_.block_at(bytecode_block.id());
        State state = bytecode_block.id() == decoder_.entry_block_id()
                          ? make_entry_state(block)
                          : make_block_entry_state(block);

        for(const BytecodeInstruction &instruction:
            bytecode_block.instructions())
        {
            switch(instruction.control_flow())
            {
                case BytecodeControlFlow::Sequential:
                    translate_sequential_instruction(block, instruction, state);
                    break;

                case BytecodeControlFlow::ConditionalJump:
                case BytecodeControlFlow::UnconditionalJump:
                case BytecodeControlFlow::Terminator:
                    translate_control_instruction(block, bytecode_block,
                                                  instruction, state);
                    break;

                case BytecodeControlFlow::Invalid:
                    fatal("invalid decoded JIT bytecode control flow");
            }

            if(!block->instructions().empty() &&
               block->instruction_at(block->instructions().size() - 1)
                   .is_block_terminator())
            {
                return;
            }
        }

        if(bytecode_block.successors().size() != 1)
        {
            fatal("sequential JIT bytecode block does not have exactly one "
                  "successor");
        }
        BlockEdge *edge =
            make_state_edge(block, bytecode_block.successors().front(), state);
        builder_.emplace_instruction<UnconditionalBranchInstruction>(block,
                                                                     edge);
    }

    void CoreBytecodeTranslator::translate_sequential_instruction(
        Block *block, const BytecodeInstruction &instruction, State &state)
    {
        std::vector<ProgramValueRef> inputs =
            state_tracker_.read(state, instruction.sources());
        std::vector<ProgramValueRef> outputs;

        switch(instruction.semantic_opcode())
        {
            case Bytecode::LdaConstant:
                {
                    assert(instruction.operands().size() == 1);
                    uint32_t index = instruction.operands()[0].value;
                    assert(index < code_object_.constant_table.size());
                    outputs.push_back(emit_constant(
                        block, code_object_.constant_table[index].value()));
                    break;
                }
            case Bytecode::LdaSmi:
                assert(instruction.operands().size() == 1);
                outputs.push_back(emit_constant(
                    block,
                    Value::from_smi(instruction.operands()[0].signed_value())));
                break;
            case Bytecode::LdaTrue:
                outputs.push_back(emit_constant(block, Value::True()));
                break;
            case Bytecode::LdaFalse:
                outputs.push_back(emit_constant(block, Value::False()));
                break;
            case Bytecode::LdaNone:
                outputs.push_back(emit_constant(block, Value::None()));
                break;

            case Bytecode::Ldar:
            case Bytecode::Star:
            case Bytecode::Mov:
                assert(inputs.size() == 1);
                outputs.push_back(inputs.front());
                break;

            case Bytecode::TestIs:
                assert(inputs.size() == 2);
                outputs.emplace_back(
                    builder_.emplace_instruction<IsInstruction>(
                        block, tagged(inputs[0]), tagged(inputs[1])));
                break;

            case Bytecode::TestIsNot:
                assert(inputs.size() == 2);
                outputs.emplace_back(
                    builder_.emplace_instruction<IsNotInstruction>(
                        block, tagged(inputs[0]), tagged(inputs[1])));
                break;

            case Bytecode::Add:
                {
                    const OperatorInlineCache *cache =
                        instruction.operator_cache();
                    assert(cache != nullptr);
                    if(!cache->empty())
                    {
                        emit_unsupported(block, instruction, state);
                        return;
                    }

                    assert(inputs.size() == 2);
                    SnapshotRef snapshot =
                        emit_snapshot(block, instruction.pc_offset(), state);
                    InlineTagGuardInstruction lhs =
                        builder_.emplace_instruction<InlineTagGuardInstruction>(
                            block, tagged(inputs[0]), snapshot,
                            InlineValueClass::SMI);
                    InlineTagGuardInstruction rhs =
                        builder_.emplace_instruction<InlineTagGuardInstruction>(
                            block, tagged(inputs[1]), snapshot,
                            InlineValueClass::SMI);
                    outputs.emplace_back(
                        builder_.emplace_instruction<AddSMIInstruction>(
                            block, TaggedValueRef(lhs), TaggedValueRef(rhs),
                            snapshot));
                    break;
                }

            case Bytecode::AddSmi:
                {
                    const OperatorInlineCache *cache =
                        instruction.operator_cache();
                    assert(cache != nullptr);
                    if(!cache->empty())
                    {
                        emit_unsupported(block, instruction, state);
                        return;
                    }

                    assert(inputs.size() == 1);
                    assert(instruction.operands().size() == 2);
                    SnapshotRef snapshot =
                        emit_snapshot(block, instruction.pc_offset(), state);
                    InlineTagGuardInstruction lhs =
                        builder_.emplace_instruction<InlineTagGuardInstruction>(
                            block, tagged(inputs[0]), snapshot,
                            InlineValueClass::SMI);
                    ProgramValueRef rhs = emit_constant(
                        block, Value::from_smi(
                                   instruction.operands()[0].signed_value()));
                    outputs.emplace_back(
                        builder_.emplace_instruction<AddSMIInstruction>(
                            block, TaggedValueRef(lhs), tagged(rhs), snapshot));
                    break;
                }

            case Bytecode::Nop:
                break;

            default:
                emit_unsupported(block, instruction, state);
                return;
        }

        state_tracker_.write(state, instruction.destinations(), outputs);
    }

    void CoreBytecodeTranslator::translate_control_instruction(
        Block *block, const BytecodeBlock &bytecode_block,
        const BytecodeInstruction &instruction, State &state)
    {
        std::vector<ProgramValueRef> inputs =
            state_tracker_.read(state, instruction.sources());

        switch(instruction.semantic_opcode())
        {
            case Bytecode::Return:
                assert(inputs.size() == 1);
                builder_.emplace_instruction<ReturnInstruction>(
                    block, tagged(inputs.front()),
                    pointer(state_tracker_.value_at(
                        state, BytecodeValueLocation::stack_slot(
                                   FrameHeaderPreviousFpOffset))),
                    tagged(state_tracker_.value_at(
                        state, BytecodeValueLocation::stack_slot(
                                   FrameHeaderReturnCodeObjectOffset))),
                    pointer(state_tracker_.value_at(
                        state, BytecodeValueLocation::stack_slot(
                                   FrameHeaderReturnPcOffset))));
                return;

            case Bytecode::Jump:
                {
                    assert(bytecode_block.successors().size() == 1);
                    BlockEdge *edge = make_state_edge(
                        block, bytecode_block.successors().front(), state);
                    builder_
                        .emplace_instruction<UnconditionalBranchInstruction>(
                            block, edge);
                    return;
                }

            case Bytecode::JumpIfTrue:
                {
                    assert(inputs.size() == 1);
                    assert(bytecode_block.successors().size() == 2);
                    BlockEdge *false_edge = make_state_edge(
                        block, bytecode_block.successors()[0], state);
                    BlockEdge *true_edge = make_state_edge(
                        block, bytecode_block.successors()[1], state);
                    builder_.emplace_instruction<ConditionalBranchInstruction>(
                        block, tagged(inputs.front()), true_edge, false_edge);
                    return;
                }

            case Bytecode::JumpIfFalse:
                {
                    assert(inputs.size() == 1);
                    assert(bytecode_block.successors().size() == 2);
                    BlockEdge *true_edge = make_state_edge(
                        block, bytecode_block.successors()[0], state);
                    BlockEdge *false_edge = make_state_edge(
                        block, bytecode_block.successors()[1], state);
                    builder_.emplace_instruction<ConditionalBranchInstruction>(
                        block, tagged(inputs.front()), true_edge, false_edge);
                    return;
                }

            default:
                break;
        }

        emit_unsupported(block, instruction, state);
    }

    void CoreBytecodeTranslator::emit_unsupported(
        Block *block, const BytecodeInstruction &instruction,
        const State &pre_instruction_state)
    {
        SnapshotRef snapshot = emit_snapshot(block, instruction.pc_offset(),
                                             pre_instruction_state);
        builder_.emplace_instruction<ResumeInInterpreterInstruction>(block,
                                                                     snapshot);
    }

    SnapshotRef CoreBytecodeTranslator::emit_snapshot(
        Block *block, BytecodePCOffset resume_pc_offset, const State &state)
    {
        std::vector<ProgramValueRef> captured = capture_snapshot_values(state);
        return SnapshotRef(builder_.emplace_instruction<SnapshotInstruction>(
            block, std::span<const ProgramValueRef>(captured),
            resume_pc_offset));
    }

    std::vector<ProgramValueRef>
    CoreBytecodeTranslator::capture_snapshot_values(const State &state) const
    {
        std::span<const ProgramValueRef> values = state_tracker_.values(state);
        return {values.begin(), values.end()};
    }

    ProgramValueRef CoreBytecodeTranslator::emit_constant(Block *block,
                                                          Value value)
    {
        Value retained = builder_.retain_and_pin_value(value);
        return ProgramValueRef(
            builder_.emplace_instruction<ConstInstruction>(block, retained));
    }

    BlockEdge *CoreBytecodeTranslator::make_state_edge(Block *source,
                                                       BytecodeBlockId target,
                                                       const State &state)
    {
        std::span<const ProgramValueRef> arguments =
            state_tracker_.block_arguments(state);
        return builder_.make_block_edge(source, builder_.block_at(target),
                                        arguments);
    }

}  // namespace cl::jit
