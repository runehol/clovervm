#include "jit/compilation_arena.h"

#include <array>
#include <cassert>

namespace cl::jit
{
    ProgramValueRef CompilationArena::make_parameter()
    {
        return ProgramValueRef(
            make_instruction(InstructionKind::Parameter, 0, false, {}));
    }

    ProgramValueRef
    CompilationArena::make_synthesize_immediate(InlineValueConstant value)
    {
        std::array<Instruction::Slot, 1> slots = {
            static_cast<uintptr_t>(value.value().as.integer)};
        return ProgramValueRef(make_instruction(
            InstructionKind::SynthesizeImmediate, 0, false, slots));
    }

    SnapshotRef CompilationArena::make_snapshot(uint32_t resume_pc)
    {
        std::array<Instruction::Slot, 2> slots = {0, resume_pc};
        return SnapshotRef(
            make_instruction(InstructionKind::Snapshot, 0, true, slots));
    }

    ProgramValueRef CompilationArena::make_add_smi(ProgramValueOperand lhs,
                                                   ProgramValueOperand rhs,
                                                   SnapshotRef snapshot)
    {
        std::array<Instruction::Slot, 3> slots = {
            lhs.raw_word(), rhs.raw_word(),
            instruction_reference_word(snapshot.instruction())};
        return ProgramValueRef(
            make_instruction(InstructionKind::AddSMI, 3, false, slots));
    }

    ProgramValueRef CompilationArena::make_python_call(
        ProgramValueOperand callable,
        absl::Span<const ProgramValueOperand> arguments, SnapshotRef snapshot,
        uint32_t interpreter_return_pc)
    {
        assert(arguments.size() <= Instruction::OperandCountMask - 2);
        size_t operand_count = arguments.size() + 2;
        absl::Span<uintptr_t> operand_words =
            instruction_side_data_.allocate_words(operand_count);
        operand_words[0] = callable.raw_word();
        operand_words[1] = instruction_reference_word(snapshot.instruction());
        for(size_t index = 0; index < arguments.size(); ++index)
        {
            operand_words[index + 2] = arguments[index].raw_word();
        }

        std::array<Instruction::Slot, 2> slots = {
            reinterpret_cast<uintptr_t>(operand_words.data()),
            interpreter_return_pc};
        return ProgramValueRef(make_instruction(
            InstructionKind::PythonCall, static_cast<uint16_t>(operand_count),
            true, slots));
    }

    Instruction *
    CompilationArena::make_conditional_branch(ProgramValueOperand condition,
                                              BlockEdge *true_edge,
                                              BlockEdge *false_edge)
    {
        assert(true_edge != nullptr);
        assert(false_edge != nullptr);
        std::array<Instruction::Slot, 3> slots = {
            condition.raw_word(), reinterpret_cast<uintptr_t>(true_edge),
            reinterpret_cast<uintptr_t>(false_edge)};
        return make_instruction(InstructionKind::ConditionalBranch, 1, false,
                                slots);
    }

    Instruction *CompilationArena::make_unconditional_branch(BlockEdge *edge)
    {
        assert(edge != nullptr);
        std::array<Instruction::Slot, 1> slots = {
            reinterpret_cast<uintptr_t>(edge)};
        return make_instruction(InstructionKind::UnconditionalBranch, 0, false,
                                slots);
    }

    Instruction *CompilationArena::make_return(ProgramValueOperand value)
    {
        std::array<Instruction::Slot, 1> slots = {value.raw_word()};
        return make_instruction(InstructionKind::Return, 1, false, slots);
    }

}  // namespace cl::jit
