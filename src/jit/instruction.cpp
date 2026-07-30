#include "jit/instruction.h"

#include "jit/compilation_storage.h"
#include "runtime/fatal.h"

#include <array>
#include <cassert>

namespace cl::jit
{
    [[noreturn]] void InstructionEntry::fatal_poisoned_access()
    {
        fatal("attempted to access a poisoned JIT instruction");
    }

    const InstructionEntry &Instruction::entry() const
    {
        return storage_->instruction_entry(id_);
    }

    bool Instruction::is_poisoned() const { return entry().is_poisoned(); }

    InstructionKind Instruction::kind() const { return entry().kind(); }

    uint16_t Instruction::operand_count() const
    {
        return entry().operand_count();
    }

    bool Instruction::operands_are_indirect() const
    {
        return entry().operands_are_indirect();
    }

    Instruction::Slot Instruction::slot(size_t index) const
    {
        return entry().slot(index);
    }

    Instruction::Slot Instruction::operand_word(size_t index) const
    {
        assert(index < operand_count());
        if(!operands_are_indirect())
        {
            return slot(index);
        }
        return storage_->instruction_operands(slot(IndirectOperandSlot),
                                              operand_count())[index];
    }

    namespace
    {
        InstructionKindMetadata make_instruction_kind_metadata(
            IRLevelMask allowed_ir_levels, EffectProfile must_effects,
            EffectProfile may_effects, uint32_t side_exit_argument_start,
            uint8_t fixed_operand_count, uint8_t attribute_count,
            uint8_t inline_slot_count, bool has_variadic_operands,
            bool operands_are_indirect)
        {
            assert(inline_slot_count <= Instruction::InlineSlotCount);
            assert(has_effects(may_effects, must_effects));
            assert(!has_effects(must_effects, EffectProfile::TerminateBlock) ||
                   has_effects(must_effects, EffectProfile::ControlFlow));
            assert(!has_effects(may_effects, EffectProfile::TerminateBlock) ||
                   has_effects(may_effects, EffectProfile::ControlFlow));
            assert(side_exit_argument_start ==
                       InstructionKindMetadata::NoSideExitArguments ||
                   (has_variadic_operands &&
                    side_exit_argument_start == fixed_operand_count));
            return {allowed_ir_levels,    must_effects,
                    may_effects,          side_exit_argument_start,
                    fixed_operand_count,  attribute_count,
                    inline_slot_count,    has_variadic_operands,
                    operands_are_indirect};
        }

        InstructionKindMetadata metadata_for(InstructionKind kind)
        {
            switch(kind)
            {
#define CL_JIT_IR_LEVELS(set) IRLevelMask::set
#define CL_JIT_RESULT(...)
#define CL_JIT_EFFECT_BOUNDS(must_effects, may_effects)                        \
    EffectProfile::must_effects, EffectProfile::may_effects
#define CL_JIT_EFFECT_BOUNDS_MAY_TWO(must_effects, may_first, may_second)      \
    EffectProfile::must_effects,                                               \
        EffectProfile::may_first | EffectProfile::may_second
#define CL_JIT_EXACT_EFFECTS_TWO(first, second)                                \
    EffectProfile::first | EffectProfile::second,                              \
        EffectProfile::first | EffectProfile::second
#define CL_JIT_EXACT_EFFECTS_THREE(first, second, third)                       \
    EffectProfile::first | EffectProfile::second | EffectProfile::third,       \
        EffectProfile::first | EffectProfile::second | EffectProfile::third
#define CL_JIT_COUNT_FIXED_OPERAND(...)                                        \
    (assert(!has_variadic_operands &&                                          \
            "fixed operands must precede the variadic range"),                 \
     ++fixed_operand_count);
#define CL_JIT_COUNT_VARIADIC_OPERAND(...)                                     \
    (assert(!has_variadic_operands &&                                          \
            "an instruction may have only one variadic range"),                \
     has_variadic_operands = true);
#define CL_JIT_COUNT_PROGRAM_VALUES(name, role)                                \
    CL_JIT_COUNT_PROGRAM_VALUES_##role(name)
#define CL_JIT_COUNT_PROGRAM_VALUES_Snapshot(name)                             \
    CL_JIT_COUNT_VARIADIC_OPERAND(name)
#define CL_JIT_COUNT_PROGRAM_VALUES_SideExit(name)                             \
    (assert(!has_variadic_operands &&                                          \
            "an instruction may have only one variadic range"),                \
     side_exit_argument_start = fixed_operand_count,                           \
     has_variadic_operands = true);
#define CL_JIT_COUNT_ATTRIBUTE(...) (++attribute_count);
#define CL_JIT_COUNT_ATTRIBUTE_WORDS(name, attribute_class)                    \
    attribute_word_count +=                                                    \
        sizeof(InstructionAttributeStorage_##attribute_class) /                \
        sizeof(Instruction::Slot);
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    case InstructionKind::name:                                                \
        {                                                                      \
            uint8_t fixed_operand_count = 0;                                   \
            uint8_t attribute_count = 0;                                       \
            uint8_t attribute_word_count = 0;                                  \
            uint8_t inline_slot_count = 0;                                     \
            bool has_variadic_operands = false;                                \
            uint32_t side_exit_argument_start =                                \
                InstructionKindMetadata::NoSideExitArguments;                  \
            operands(CL_JIT_COUNT_FIXED_OPERAND,                               \
                     CL_JIT_COUNT_VARIADIC_OPERAND,                            \
                     CL_JIT_COUNT_PROGRAM_VALUES)                              \
                attributes(CL_JIT_COUNT_ATTRIBUTE) attributes(                 \
                    CL_JIT_COUNT_ATTRIBUTE_WORDS) bool operands_are_indirect = \
                    name##Instruction::OperandsAreIndirect;                    \
            inline_slot_count =                                                \
                operands_are_indirect                                          \
                    ? Instruction::InlineSlotCount                             \
                    : fixed_operand_count + attribute_word_count;              \
            return make_instruction_kind_metadata(                             \
                ir_levels, effects, side_exit_argument_start,                  \
                fixed_operand_count, attribute_count, inline_slot_count,       \
                has_variadic_operands, operands_are_indirect);                 \
        }
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_COUNT_ATTRIBUTE_WORDS
#undef CL_JIT_COUNT_ATTRIBUTE
#undef CL_JIT_COUNT_PROGRAM_VALUES_SideExit
#undef CL_JIT_COUNT_PROGRAM_VALUES_Snapshot
#undef CL_JIT_COUNT_PROGRAM_VALUES
#undef CL_JIT_COUNT_VARIADIC_OPERAND
#undef CL_JIT_COUNT_FIXED_OPERAND
#undef CL_JIT_EXACT_EFFECTS_THREE
#undef CL_JIT_EXACT_EFFECTS_TWO
#undef CL_JIT_EFFECT_BOUNDS_MAY_TWO
#undef CL_JIT_EFFECT_BOUNDS
#undef CL_JIT_RESULT
#undef CL_JIT_IR_LEVELS
            }
            assert(false);
            return {};
        }

        const std::array<InstructionKindMetadata,
                         static_cast<size_t>(InstructionOrdinal::Count)>
            instruction_metadata = {{
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    metadata_for(InstructionKind::name),
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
            }};
    }  // namespace

    const InstructionKindMetadata &
    instruction_kind_metadata(InstructionKind kind)
    {
        assert(is_valid_instruction_kind(kind));
        size_t index = static_cast<size_t>(instruction_ordinal(kind));
        assert(index < instruction_metadata.size());
        return instruction_metadata[index];
    }

    TerminatorInstruction::BlockSuccessorEdges
    TerminatorInstruction::block_successor_edges() const
    {
        switch(instruction_.kind())
        {
            case InstructionKind::ConditionalBranch:
                {
                    auto branch =
                        instruction_.as<ConditionalBranchInstruction>();
                    return {branch.true_edge(), branch.false_edge()};
                }
            case InstructionKind::UnconditionalBranch:
                return {
                    instruction_.as<UnconditionalBranchInstruction>().edge()};
            case InstructionKind::Return:
            case InstructionKind::ResumeInInterpreter:
            case InstructionKind::ResumeInInterpreterWithSideExitRegion:
                return {};
            default:
                break;
        }
        assert(false);
        return {};
    }

}  // namespace cl::jit
