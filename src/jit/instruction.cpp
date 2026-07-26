#include "jit/instruction.h"

#include "jit/compilation_storage.h"
#include "runtime/fatal.h"

#include <array>
#include <cassert>

namespace cl::jit
{
    [[noreturn]] void InstructionEntry::fatal_detached_access()
    {
        fatal("attempted to access a detached JIT instruction");
    }

    const InstructionEntry &Instruction::entry() const
    {
        return storage_->instruction_entry(id_);
    }

    bool Instruction::is_detached() const { return entry().is_detached(); }

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

    namespace
    {
        InstructionKindMetadata make_instruction_kind_metadata(
            IRLevelMask allowed_ir_levels, EffectProfile must_effects,
            EffectProfile may_effects, uint8_t fixed_operand_count,
            uint8_t attribute_count, uint8_t inline_slot_count,
            bool has_variadic_operands)
        {
            assert(inline_slot_count <= Instruction::InlineSlotCount);
            assert(has_effects(may_effects, must_effects));
            assert(!has_effects(must_effects, EffectProfile::TerminateBlock) ||
                   has_effects(must_effects, EffectProfile::ControlFlow));
            assert(!has_effects(may_effects, EffectProfile::TerminateBlock) ||
                   has_effects(may_effects, EffectProfile::ControlFlow));
            return {allowed_ir_levels,    must_effects,    may_effects,
                    fixed_operand_count,  attribute_count, inline_slot_count,
                    has_variadic_operands};
        }

        InstructionKindMetadata metadata_for(InstructionKind kind)
        {
            switch(kind)
            {
#define CL_JIT_IR_LEVELS_ONE(first) IRLevelMask::first
#define CL_JIT_IR_LEVELS_TWO(first, second)                                    \
    (IRLevelMask::first | IRLevelMask::second)
#define CL_JIT_IR_LEVELS_THREE(first, second, third)                           \
    (IRLevelMask::first | IRLevelMask::second | IRLevelMask::third)
#define CL_JIT_SELECT_IR_LEVELS(_1, _2, _3, selected, ...) selected
#define CL_JIT_IR_LEVELS(...)                                                  \
    CL_JIT_SELECT_IR_LEVELS(__VA_ARGS__, CL_JIT_IR_LEVELS_THREE,               \
                            CL_JIT_IR_LEVELS_TWO,                              \
                            CL_JIT_IR_LEVELS_ONE)(__VA_ARGS__)
#define CL_JIT_RESULT(...)
#define CL_JIT_EFFECT_BOUNDS(must_effects, may_effects)                        \
    EffectProfile::must_effects, EffectProfile::may_effects
#define CL_JIT_EFFECT_BOUNDS_MAY_TWO(must_effects, may_first, may_second)      \
    EffectProfile::must_effects,                                               \
        EffectProfile::may_first | EffectProfile::may_second
#define CL_JIT_EXACT_EFFECTS_TWO(first, second)                                \
    EffectProfile::first | EffectProfile::second,                              \
        EffectProfile::first | EffectProfile::second
#define CL_JIT_COUNT_FIXED_OPERAND(...)                                        \
    (assert(!has_variadic_operands &&                                          \
            "fixed operands must precede the variadic range"),                 \
     ++fixed_operand_count);
#define CL_JIT_COUNT_VARIADIC_OPERAND(...)                                     \
    (assert(!has_variadic_operands &&                                          \
            "an instruction may have only one variadic range"),                \
     has_variadic_operands = true);
#define CL_JIT_COUNT_SNAPSHOT_VALUES(...)                                      \
    CL_JIT_COUNT_VARIADIC_OPERAND(__VA_ARGS__)
#define CL_JIT_COUNT_ATTRIBUTE(...) (++attribute_count);
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    case InstructionKind::name:                                                \
        {                                                                      \
            uint8_t fixed_operand_count = 0;                                   \
            uint8_t attribute_count = 0;                                       \
            uint8_t inline_slot_count = 0;                                     \
            bool has_variadic_operands = false;                                \
            operands(CL_JIT_COUNT_FIXED_OPERAND,                               \
                     CL_JIT_COUNT_VARIADIC_OPERAND,                            \
                     CL_JIT_COUNT_SNAPSHOT_VALUES)                             \
                attributes(CL_JIT_COUNT_ATTRIBUTE) inline_slot_count =         \
                    (has_variadic_operands ? 1 : fixed_operand_count) +        \
                    attribute_count;                                           \
            return make_instruction_kind_metadata(                             \
                ir_levels, effects, fixed_operand_count, attribute_count,      \
                inline_slot_count, has_variadic_operands);                     \
        }
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_COUNT_ATTRIBUTE
#undef CL_JIT_COUNT_SNAPSHOT_VALUES
#undef CL_JIT_COUNT_VARIADIC_OPERAND
#undef CL_JIT_COUNT_FIXED_OPERAND
#undef CL_JIT_EXACT_EFFECTS_TWO
#undef CL_JIT_EFFECT_BOUNDS_MAY_TWO
#undef CL_JIT_EFFECT_BOUNDS
#undef CL_JIT_RESULT
#undef CL_JIT_IR_LEVELS
#undef CL_JIT_SELECT_IR_LEVELS
#undef CL_JIT_IR_LEVELS_THREE
#undef CL_JIT_IR_LEVELS_TWO
#undef CL_JIT_IR_LEVELS_ONE
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
                return {};
            default:
                break;
        }
        assert(false);
        return {};
    }

}  // namespace cl::jit
