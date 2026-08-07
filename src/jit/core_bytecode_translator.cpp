#include "jit/core_bytecode_translator.h"

#include "object_model/validity_cell.h"
#include "runtime/fatal.h"
#include "runtime/virtual_machine.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
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
            case Bytecode::AddSmi:
                if(!lower_binary_arithmetic(
                       block, instruction,
                       BinaryArithmeticSMIWithSnapshotSubkind::AddSMI, inputs,
                       state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::Sub:
            case Bytecode::SubSmi:
                if(!lower_binary_arithmetic(
                       block, instruction,
                       BinaryArithmeticSMIWithSnapshotSubkind::SubSMI, inputs,
                       state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::Mul:
            case Bytecode::MulSmi:
                if(!lower_binary_arithmetic(
                       block, instruction,
                       BinaryArithmeticSMIWithSnapshotSubkind::MulSMI, inputs,
                       state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::And:
            case Bytecode::AndSmi:
                if(!lower_binary_logical(block, instruction,
                                         BinaryLogicalSMISubkind::AndSMI,
                                         inputs, state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::Or:
            case Bytecode::OrSmi:
                if(!lower_binary_logical(block, instruction,
                                         BinaryLogicalSMISubkind::OrrSMI,
                                         inputs, state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::Xor:
            case Bytecode::XorSmi:
                if(!lower_binary_logical(block, instruction,
                                         BinaryLogicalSMISubkind::EorSMI,
                                         inputs, state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::TestEqual:
                if(!lower_binary_comparison(
                       block, instruction, BinaryComparisonSMISubkind::EqualSMI,
                       inputs, state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::TestNotEqual:
                if(!lower_binary_comparison(
                       block, instruction,
                       BinaryComparisonSMISubkind::NotEqualSMI, inputs, state,
                       outputs))
                {
                    return;
                }
                break;

            case Bytecode::TestLess:
                if(!lower_binary_comparison(block, instruction,
                                            BinaryComparisonSMISubkind::LessSMI,
                                            inputs, state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::TestLessEqual:
                if(!lower_binary_comparison(
                       block, instruction,
                       BinaryComparisonSMISubkind::LessEqualSMI, inputs, state,
                       outputs))
                {
                    return;
                }
                break;

            case Bytecode::TestGreater:
                if(!lower_binary_comparison(
                       block, instruction,
                       BinaryComparisonSMISubkind::GreaterSMI, inputs, state,
                       outputs))
                {
                    return;
                }
                break;

            case Bytecode::TestGreaterEqual:
                if(!lower_binary_comparison(
                       block, instruction,
                       BinaryComparisonSMISubkind::GreaterEqualSMI, inputs,
                       state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::Neg:
            case Bytecode::Pos:
            case Bytecode::Invert:
            case Bytecode::TernaryPow:
                if(!lower_non_fastpathed_operator(block, instruction, inputs,
                                                  state, outputs))
                {
                    return;
                }
                break;

            case Bytecode::Nop:
                break;

            default:
                emit_unsupported(block, instruction, state);
                return;
        }

        state_tracker_.write(state, instruction.destinations(), outputs);
    }

    bool CoreBytecodeTranslator::lower_non_fastpathed_operator(
        Block *block, const BytecodeInstruction &instruction,
        std::span<const ProgramValueRef> inputs, State &state,
        std::vector<ProgramValueRef> &outputs)
    {
        const OperatorInlineCache *cache = instruction.operator_cache();
        assert(cache != nullptr);
        assert(!cache->empty());

        TrustedHandlerArity arity;
        size_t argument_count;
        size_t guarded_argument_count;
        switch(bytecode_info(instruction.encoded_opcode()).compound_role)
        {
            case BytecodeCompoundRole::BinaryOperator:
                arity = TrustedHandlerArity::Binary;
                argument_count = 2;
                guarded_argument_count = 2;
                break;
            case BytecodeCompoundRole::TernaryOperator:
                arity = TrustedHandlerArity::Ternary;
                argument_count = 3;
                guarded_argument_count = 2;
                break;
            case BytecodeCompoundRole::None:
                arity = TrustedHandlerArity::Unary;
                argument_count = 1;
                guarded_argument_count = 1;
                break;
            case BytecodeCompoundRole::BinaryOperatorContinuation:
            case BytecodeCompoundRole::TernaryOperatorContinuation:
                emit_unsupported(block, instruction, state);
                return false;
        }

        if(instruction.sources().size() != argument_count ||
           cache->trusted_handler.is_null())
        {
            emit_unsupported(block, instruction, state);
            return false;
        }

        TrustedHandlerTarget handler = cache->trusted_handler.target(arity);
        std::optional<TrustedHandlerMetadata> metadata =
            vm_.trusted_handler_metadata(handler);
        if(!metadata.has_value() || metadata->arity != arity ||
           has_trusted_handler_effects(metadata->effects,
                                       TrustedHandlerEffects::Safepoint) ||
           has_trusted_handler_effects(metadata->effects,
                                       TrustedHandlerEffects::Raise) ||
           has_trusted_handler_effects(metadata->effects,
                                       TrustedHandlerEffects::CallPython))
        {
            emit_unsupported(block, instruction, state);
            return false;
        }

        assert(instruction.sources().size() == inputs.size());
        SnapshotRef snapshot =
            emit_snapshot(block, instruction.pc_offset(), state);
        for(size_t index = 0; index < guarded_argument_count; ++index)
        {
            BytecodeValueLocation source = instruction.sources()[index];
            ProgramValueRef value = state_tracker_.value_at(state, source);
            ShapeKey shape_key = cache->operand_shape_keys[index];
            if(shape_key.is_inline())
            {
                emit_inline_tag_guard(block, value, snapshot,
                                      TaggedValueClass::masked_equal(
                                          uint8_t(value_tag_mask),
                                          uint8_t(shape_key.inline_tag())),
                                      state);
            }
            else
            {
                ShapeGuardInstruction guard =
                    builder_
                        .emplace_instruction<PointerAndShapeGuardInstruction>(
                            block, tagged(value), snapshot,
                            vm_.shape_for_key(shape_key));
                state_tracker_.replace_value(state, value,
                                             ProgramValueRef(guard));
            }
        }

        for(size_t index = 0; index < guarded_argument_count; ++index)
        {
            ValidityCell *validity =
                cache->operand_lookup_validity_cells[index];
            if(validity == nullptr)
            {
                continue;
            }
            BytecodeValueLocation source = instruction.sources()[index];
            ProgramValueRef value = state_tracker_.value_at(state, source);
            ValidityCellGuardInstruction guard =
                builder_.emplace_instruction<ValidityCellGuardInstruction>(
                    block, tagged(value), snapshot, validity);
            state_tracker_.replace_value(state, value, ProgramValueRef(guard));
        }

        std::vector<TaggedValueRef> arguments;
        arguments.reserve(inputs.size());
        for(BytecodeValueLocation source: instruction.sources())
        {
            arguments.push_back(tagged(state_tracker_.value_at(state, source)));
        }
        emit_trusted_handler_call(block, *cache, *metadata, arguments, outputs);
        return true;
    }

    void CoreBytecodeTranslator::emit_trusted_handler_call(
        Block *block, const OperatorInlineCache &cache,
        const TrustedHandlerMetadata &metadata,
        std::span<const TaggedValueRef> arguments,
        std::vector<ProgramValueRef> &outputs)
    {
        assert((metadata.arity == TrustedHandlerArity::Unary &&
                arguments.size() == 1) ||
               (metadata.arity == TrustedHandlerArity::Binary &&
                arguments.size() == 2) ||
               (metadata.arity == TrustedHandlerArity::Ternary &&
                arguments.size() == 3));
        if(std::optional<ProgramValueRef> specialized =
               try_emit_exact_float_binary(block, cache, metadata, arguments))
        {
            outputs.push_back(*specialized);
            return;
        }
        TrustedHandlerTarget handler =
            cache.trusted_handler.target(metadata.arity);
        outputs.emplace_back(
            builder_.emplace_instruction<TrustedHandlerCallInstruction>(
                block, arguments, handler));
    }

    std::optional<ProgramValueRef>
    CoreBytecodeTranslator::try_emit_exact_float_binary(
        Block *block, const OperatorInlineCache &cache,
        const TrustedHandlerMetadata &metadata,
        std::span<const TaggedValueRef> arguments)
    {
        if(metadata.arity != TrustedHandlerArity::Binary ||
           arguments.size() != 2)
        {
            return std::nullopt;
        }

        ShapeKey float_shape_key =
            ShapeKey::from_shape(vm_.float_class()->get_instance_root_shape());
        if(cache.operand_shape_keys[0] != float_shape_key ||
           cache.operand_shape_keys[1] != float_shape_key)
        {
            return std::nullopt;
        }

        std::optional<BinaryArithmeticF64Subkind> arithmetic;
        std::optional<BinaryComparisonF64Subkind> comparison;
        switch(metadata.semantics)
        {
            case TrustedHandlerSemantics::Add:
                arithmetic = BinaryArithmeticF64Subkind::AddF64;
                break;
            case TrustedHandlerSemantics::Sub:
                arithmetic = BinaryArithmeticF64Subkind::SubF64;
                break;
            case TrustedHandlerSemantics::Mul:
                arithmetic = BinaryArithmeticF64Subkind::MulF64;
                break;
            case TrustedHandlerSemantics::Equal:
                comparison = BinaryComparisonF64Subkind::EqualF64;
                break;
            case TrustedHandlerSemantics::NotEqual:
                comparison = BinaryComparisonF64Subkind::NotEqualF64;
                break;
            case TrustedHandlerSemantics::Less:
                comparison = BinaryComparisonF64Subkind::LessF64;
                break;
            case TrustedHandlerSemantics::LessEqual:
                comparison = BinaryComparisonF64Subkind::LessEqualF64;
                break;
            case TrustedHandlerSemantics::Greater:
                comparison = BinaryComparisonF64Subkind::GreaterF64;
                break;
            case TrustedHandlerSemantics::GreaterEqual:
                comparison = BinaryComparisonF64Subkind::GreaterEqualF64;
                break;
            default:
                return std::nullopt;
        }

        UnboxF64Instruction lhs =
            builder_.emplace_instruction<UnboxF64Instruction>(block,
                                                              arguments[0]);
        UnboxF64Instruction rhs =
            builder_.emplace_instruction<UnboxF64Instruction>(block,
                                                              arguments[1]);
        if(arithmetic.has_value())
        {
            BinaryArithmeticF64Instruction result =
                builder_.emplace_instruction<BinaryArithmeticF64Instruction>(
                    block, *arithmetic, F64Ref(lhs), F64Ref(rhs));
            return ProgramValueRef(
                builder_.emplace_instruction<BoxF64Instruction>(
                    block, F64Ref(result)));
        }

        assert(comparison.has_value());
        return ProgramValueRef(
            builder_.emplace_instruction<BinaryComparisonF64Instruction>(
                block, *comparison, F64Ref(lhs), F64Ref(rhs)));
    }

    bool CoreBytecodeTranslator::lower_binary_arithmetic(
        Block *block, const BytecodeInstruction &instruction,
        BinaryArithmeticSMIWithSnapshotSubkind subkind,
        std::span<const ProgramValueRef> inputs, State &state,
        std::vector<ProgramValueRef> &outputs)
    {
        const OperatorInlineCache *cache = instruction.operator_cache();
        assert(cache != nullptr);
        if(!cache->empty())
        {
            return lower_non_fastpathed_operator(block, instruction, inputs,
                                                 state, outputs);
        }

        assert(inputs.size() == 1 || inputs.size() == 2);
        const bool has_immediate_rhs = inputs.size() == 1;
        if(has_immediate_rhs)
        {
            assert(instruction.operands().size() == 2);
        }

        SnapshotRef snapshot =
            emit_snapshot(block, instruction.pc_offset(), state);
        InlineTagGuardInstruction lhs = emit_inline_tag_guard(
            block, state_tracker_.value_at(state, instruction.sources()[0]),
            snapshot, TaggedValueClass::smi(), state);
        TaggedValueRef rhs = [&] {
            if(has_immediate_rhs)
            {
                return tagged(emit_constant(
                    block,
                    Value::from_smi(instruction.operands()[0].signed_value())));
            }
            return TaggedValueRef(emit_inline_tag_guard(
                block, state_tracker_.value_at(state, instruction.sources()[1]),
                snapshot, TaggedValueClass::smi(), state));
        }();

        outputs.emplace_back(builder_.emplace_instruction<
                             BinaryArithmeticSMIWithSnapshotInstruction>(
            block, subkind, TaggedValueRef(lhs), rhs, snapshot));
        return true;
    }

    bool CoreBytecodeTranslator::lower_binary_logical(
        Block *block, const BytecodeInstruction &instruction,
        BinaryLogicalSMISubkind subkind,
        std::span<const ProgramValueRef> inputs, State &state,
        std::vector<ProgramValueRef> &outputs)
    {
        const OperatorInlineCache *cache = instruction.operator_cache();
        assert(cache != nullptr);
        if(!cache->empty())
        {
            return lower_non_fastpathed_operator(block, instruction, inputs,
                                                 state, outputs);
        }

        assert(inputs.size() == 1 || inputs.size() == 2);
        bool has_immediate_rhs = inputs.size() == 1;
        if(has_immediate_rhs)
        {
            assert(instruction.operands().size() == 2);
        }

        SnapshotRef snapshot =
            emit_snapshot(block, instruction.pc_offset(), state);
        InlineTagGuardInstruction lhs = emit_inline_tag_guard(
            block, state_tracker_.value_at(state, instruction.sources()[0]),
            snapshot, TaggedValueClass::smi(), state);
        TaggedValueRef rhs = [&] {
            if(has_immediate_rhs)
            {
                return tagged(emit_constant(
                    block,
                    Value::from_smi(instruction.operands()[0].signed_value())));
            }
            return TaggedValueRef(emit_inline_tag_guard(
                block, state_tracker_.value_at(state, instruction.sources()[1]),
                snapshot, TaggedValueClass::smi(), state));
        }();

        outputs.emplace_back(
            builder_.emplace_instruction<BinaryLogicalSMIInstruction>(
                block, subkind, TaggedValueRef(lhs), rhs));
        return true;
    }

    bool CoreBytecodeTranslator::lower_binary_comparison(
        Block *block, const BytecodeInstruction &instruction,
        BinaryComparisonSMISubkind subkind,
        std::span<const ProgramValueRef> inputs, State &state,
        std::vector<ProgramValueRef> &outputs)
    {
        const OperatorInlineCache *cache = instruction.operator_cache();
        assert(cache != nullptr);
        if(!cache->empty())
        {
            return lower_non_fastpathed_operator(block, instruction, inputs,
                                                 state, outputs);
        }

        assert(inputs.size() == 2);
        SnapshotRef snapshot =
            emit_snapshot(block, instruction.pc_offset(), state);
        InlineTagGuardInstruction lhs = emit_inline_tag_guard(
            block, state_tracker_.value_at(state, instruction.sources()[0]),
            snapshot, TaggedValueClass::smi(), state);
        InlineTagGuardInstruction rhs = emit_inline_tag_guard(
            block, state_tracker_.value_at(state, instruction.sources()[1]),
            snapshot, TaggedValueClass::smi(), state);
        outputs.emplace_back(
            builder_.emplace_instruction<BinaryComparisonSMIInstruction>(
                block, subkind, TaggedValueRef(lhs), TaggedValueRef(rhs)));
        return true;
    }

    InlineTagGuardInstruction CoreBytecodeTranslator::emit_inline_tag_guard(
        Block *block, ProgramValueRef value, SnapshotRef snapshot,
        TaggedValueClass expected_class, State &state)
    {
        InlineTagGuardInstruction guard =
            builder_.emplace_instruction<InlineTagGuardInstruction>(
                block, tagged(value), snapshot, expected_class);
        state_tracker_.replace_value(state, value, ProgramValueRef(guard));
        return guard;
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
            case Bytecode::JumpIfFalse:
                {
                    assert(inputs.size() == 1);
                    assert(bytecode_block.successors().size() == 2);

                    SnapshotRef snapshot =
                        emit_snapshot(block, instruction.pc_offset(), state);
                    InlineTagGuardInstruction condition = emit_inline_tag_guard(
                        block, inputs.front(), snapshot,
                        TaggedValueClass::any_inline(), state);

                    bool jump_if_true =
                        instruction.semantic_opcode() == Bytecode::JumpIfTrue;
                    size_t true_successor = jump_if_true ? 1 : 0;
                    size_t false_successor = jump_if_true ? 0 : 1;
                    BlockEdge *true_edge = make_state_edge(
                        block, bytecode_block.successors()[true_successor],
                        state);
                    BlockEdge *false_edge = make_state_edge(
                        block, bytecode_block.successors()[false_successor],
                        state);
                    builder_.emplace_instruction<ConditionalBranchInstruction>(
                        block, TaggedValueRef(condition), true_edge,
                        false_edge);
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
