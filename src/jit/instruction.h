#ifndef CL_JIT_INSTRUCTION_H
#define CL_JIT_INSTRUCTION_H

#include "jit/side_exit_id.h"
#include "object_model/shape_key.h"
#include "object_model/value.h"
#include "util/dense_id.h"

#include <absl/container/inlined_vector.h>
#include <span>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace cl
{
    class Shape;
    class ValidityCell;
}  // namespace cl

namespace cl::jit
{
    class BlockEdge;
    class CompilationStorage;
    class GraphRewriter;
    class Instruction;

    using InstructionId = DenseId<Instruction>;

    enum class ResultClass : uint8_t
    {
        None,
        ProgramValue,
        Snapshot,
        Count,
    };

    enum class OperandClass : uint8_t
    {
        ProgramValue = static_cast<uint8_t>(ResultClass::ProgramValue),
        Snapshot = static_cast<uint8_t>(ResultClass::Snapshot),
    };

    enum class ValueRepresentation : uint8_t
    {
        None,
        TaggedValue,
        F64,
        Pointer,
        Count,
    };

    enum class ValueRepresentationRequirement : uint8_t
    {
        Any,
        TaggedValue,
        F64,
        Pointer,
    };

    constexpr bool
    representation_matches(ValueRepresentationRequirement requirement,
                           ValueRepresentation representation)
    {
        switch(requirement)
        {
            case ValueRepresentationRequirement::Any:
                return representation != ValueRepresentation::None &&
                       representation != ValueRepresentation::Count;
            case ValueRepresentationRequirement::TaggedValue:
                return representation == ValueRepresentation::TaggedValue;
            case ValueRepresentationRequirement::F64:
                return representation == ValueRepresentation::F64;
            case ValueRepresentationRequirement::Pointer:
                return representation == ValueRepresentation::Pointer;
        }
        return false;
    }

    template <OperandClass Class, ValueRepresentation Representation>
    consteval ValueRepresentationRequirement
    operand_representation_requirement()
    {
        if constexpr(Class == OperandClass::Snapshot)
        {
            static_assert(Representation == ValueRepresentation::None);
            return ValueRepresentationRequirement::Any;
        }
        else
        {
            static_assert(Representation != ValueRepresentation::None);
            static_assert(Representation != ValueRepresentation::Count);
            switch(Representation)
            {
                case ValueRepresentation::TaggedValue:
                    return ValueRepresentationRequirement::TaggedValue;
                case ValueRepresentation::F64:
                    return ValueRepresentationRequirement::F64;
                case ValueRepresentation::Pointer:
                    return ValueRepresentationRequirement::Pointer;
                case ValueRepresentation::None:
                case ValueRepresentation::Count:
                    break;
            }
        }
    }

    enum class EffectProfile : uint8_t
    {
        None = 0,
        SideExit = 1 << 0,
        Allocate = 1 << 1,

        PythonVisibleEffects = 1 << 2,
        CallPython = PythonVisibleEffects,
        ControlFlow = 1 << 3,
        TerminateBlock = 1 << 4,
    };

    constexpr EffectProfile operator|(EffectProfile lhs, EffectProfile rhs)
    {
        return static_cast<EffectProfile>(static_cast<uint8_t>(lhs) |
                                          static_cast<uint8_t>(rhs));
    }

    constexpr bool operator<(EffectProfile lhs, EffectProfile rhs)
    {
        return static_cast<uint8_t>(lhs) < static_cast<uint8_t>(rhs);
    }

    constexpr bool has_effects(EffectProfile profile, EffectProfile effects)
    {
        uint8_t profile_bits = static_cast<uint8_t>(profile);
        uint8_t effect_bits = static_cast<uint8_t>(effects);
        return (profile_bits & effect_bits) == effect_bits;
    }

    enum class IRLevel : uint8_t
    {
        Semantic = 1 << 0,
        Core = 1 << 1,
        Machine = 1 << 2,
        Transition = 1 << 3,
    };

    enum class IRLevelMask : uint8_t
    {
        None = 0,
        Semantic = static_cast<uint8_t>(IRLevel::Semantic),
        Core = static_cast<uint8_t>(IRLevel::Core),
        Machine = static_cast<uint8_t>(IRLevel::Machine),
        Transition = static_cast<uint8_t>(IRLevel::Transition),
        SemanticCore = static_cast<uint8_t>(IRLevel::Semantic) |
                       static_cast<uint8_t>(IRLevel::Core),
        CoreMachine = static_cast<uint8_t>(IRLevel::Core) |
                      static_cast<uint8_t>(IRLevel::Machine),
        CoreTransition = static_cast<uint8_t>(IRLevel::Core) |
                         static_cast<uint8_t>(IRLevel::Transition),
        CoreMachineTransition = static_cast<uint8_t>(IRLevel::Core) |
                                static_cast<uint8_t>(IRLevel::Machine) |
                                static_cast<uint8_t>(IRLevel::Transition),
        SemanticCoreMachineTransition =
            static_cast<uint8_t>(IRLevel::Semantic) |
            static_cast<uint8_t>(IRLevel::Core) |
            static_cast<uint8_t>(IRLevel::Machine) |
            static_cast<uint8_t>(IRLevel::Transition),
    };

    constexpr IRLevelMask operator|(IRLevelMask lhs, IRLevelMask rhs)
    {
        return static_cast<IRLevelMask>(static_cast<uint8_t>(lhs) |
                                        static_cast<uint8_t>(rhs));
    }

    constexpr IRLevelMask ir_level_mask(IRLevel level)
    {
        return static_cast<IRLevelMask>(level);
    }

    struct InstructionResultInfo
    {
        ResultClass result_class;
        ValueRepresentation representation;
    };

    struct InstructionEffectBounds
    {
        EffectProfile must_effects;
        EffectProfile may_effects;
    };

    enum class InstructionOrdinal : uint16_t
    {
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    name,
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
        Count,
    };

    static constexpr uint16_t InstructionOrdinalMask = 0x0fff;
    static constexpr uint16_t InstructionRepresentationMask = 0x3000;
    static constexpr uint16_t InstructionResultClassMask = 0xc000;
    static constexpr unsigned InstructionRepresentationShift = 12;
    static constexpr unsigned InstructionResultClassShift = 14;

    static_assert(static_cast<uint16_t>(ResultClass::Count) <= 4);
    static_assert(static_cast<uint16_t>(ValueRepresentation::Count) <= 4);
    static_assert((InstructionOrdinalMask & InstructionRepresentationMask) ==
                  0);
    static_assert((InstructionOrdinalMask & InstructionResultClassMask) == 0);
    static_assert((InstructionRepresentationMask &
                   InstructionResultClassMask) == 0);

    constexpr uint16_t
    encode_instruction_kind(InstructionOrdinal ordinal,
                            ResultClass result_class,
                            ValueRepresentation representation)
    {
        return static_cast<uint16_t>(ordinal) |
               (static_cast<uint16_t>(representation)
                << InstructionRepresentationShift) |
               (static_cast<uint16_t>(result_class)
                << InstructionResultClassShift);
    }

    enum class InstructionKind : uint16_t
    {
#define CL_JIT_RESULT(result_class, representation)                            \
    ResultClass::result_class, ValueRepresentation::representation
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    name = encode_instruction_kind(InstructionOrdinal::name, result),
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_RESULT
    };

    // clang-format off
#define CL_JIT_LEVEL_KIND_JOIN_INNER(first, second) first##second
#define CL_JIT_LEVEL_KIND_JOIN(first, second)                                 \
    CL_JIT_LEVEL_KIND_JOIN_INNER(first, second)
#define CL_JIT_IR_LEVEL_MEMBERS(set, callback, name)                          \
    CL_JIT_LEVEL_KIND_JOIN(CL_JIT_IR_LEVEL_MEMBERS_, set)(callback, name)
#define CL_JIT_IR_LEVEL_MEMBERS_Semantic(callback, name)                      \
    callback(name, Semantic)
#define CL_JIT_IR_LEVEL_MEMBERS_Core(callback, name)                          \
    callback(name, Core)
#define CL_JIT_IR_LEVEL_MEMBERS_Machine(callback, name)                       \
    callback(name, Machine)
#define CL_JIT_IR_LEVEL_MEMBERS_Transition(callback, name)                    \
    callback(name, Transition)
#define CL_JIT_IR_LEVEL_MEMBERS_SemanticCore(callback, name)                  \
    callback(name, Semantic) callback(name, Core)
#define CL_JIT_IR_LEVEL_MEMBERS_CoreMachine(callback, name)                   \
    callback(name, Core) callback(name, Machine)
#define CL_JIT_IR_LEVEL_MEMBERS_CoreTransition(callback, name)                \
    callback(name, Core) callback(name, Transition)
#define CL_JIT_IR_LEVEL_MEMBERS_CoreMachineTransition(callback, name)         \
    callback(name, Core) callback(name, Machine) callback(name, Transition)
#define CL_JIT_IR_LEVEL_MEMBERS_SemanticCoreMachineTransition(callback, name) \
    callback(name, Semantic) callback(name, Core) callback(name, Machine)     \
        callback(name, Transition)
#define CL_JIT_LEVEL_KIND_MEMBER(name, level)                                 \
    CL_JIT_LEVEL_KIND_JOIN(CL_JIT_LEVEL_KIND_MEMBER_, level)(name)
#define CL_JIT_IR_LEVELS(set) set
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,        \
                           attributes)                                        \
    CL_JIT_IR_LEVEL_MEMBERS(                                                   \
        ir_levels, CL_JIT_LEVEL_KIND_MEMBER, name)

#define CL_JIT_LEVEL_KIND_MEMBER_Semantic(name)                               \
    name = static_cast<uint16_t>(InstructionKind::name),
#define CL_JIT_LEVEL_KIND_MEMBER_Core(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Machine(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Transition(name)
    enum class SemanticInstructionKind : uint16_t
    {
#include "jit/instruction.def"
    };
#undef CL_JIT_LEVEL_KIND_MEMBER_Transition
#undef CL_JIT_LEVEL_KIND_MEMBER_Machine
#undef CL_JIT_LEVEL_KIND_MEMBER_Core
#undef CL_JIT_LEVEL_KIND_MEMBER_Semantic

#define CL_JIT_LEVEL_KIND_MEMBER_Semantic(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Core(name)                                   \
    name = static_cast<uint16_t>(InstructionKind::name),
#define CL_JIT_LEVEL_KIND_MEMBER_Machine(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Transition(name)
    enum class CoreInstructionKind : uint16_t
    {
#include "jit/instruction.def"
    };
#undef CL_JIT_LEVEL_KIND_MEMBER_Transition
#undef CL_JIT_LEVEL_KIND_MEMBER_Machine
#undef CL_JIT_LEVEL_KIND_MEMBER_Core
#undef CL_JIT_LEVEL_KIND_MEMBER_Semantic

#define CL_JIT_LEVEL_KIND_MEMBER_Semantic(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Core(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Machine(name)                                \
    name = static_cast<uint16_t>(InstructionKind::name),
#define CL_JIT_LEVEL_KIND_MEMBER_Transition(name)
    enum class MachineInstructionKind : uint16_t
    {
#include "jit/instruction.def"
    };
#undef CL_JIT_LEVEL_KIND_MEMBER_Transition
#undef CL_JIT_LEVEL_KIND_MEMBER_Machine
#undef CL_JIT_LEVEL_KIND_MEMBER_Core
#undef CL_JIT_LEVEL_KIND_MEMBER_Semantic

#define CL_JIT_LEVEL_KIND_MEMBER_Semantic(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Core(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Machine(name)
#define CL_JIT_LEVEL_KIND_MEMBER_Transition(name)                             \
    name = static_cast<uint16_t>(InstructionKind::name),
    enum class TransitionInstructionKind : uint16_t
    {
#include "jit/instruction.def"
        // These kinds use TransitionInstruction's handwritten payload rather
        // than the graph instruction schema.
        BeginTransition = InstructionOrdinalMask - 2,
        Transfer = InstructionOrdinalMask - 1,
        ResumeInterpreter = InstructionOrdinalMask,
    };
    static_assert(static_cast<uint16_t>(InstructionOrdinal::Count) <=
                  static_cast<uint16_t>(
                      TransitionInstructionKind::BeginTransition));
#undef CL_JIT_LEVEL_KIND_MEMBER_Transition
#undef CL_JIT_LEVEL_KIND_MEMBER_Machine
#undef CL_JIT_LEVEL_KIND_MEMBER_Core
#undef CL_JIT_LEVEL_KIND_MEMBER_Semantic

#undef CL_JIT_INSTRUCTION
#undef CL_JIT_IR_LEVELS
#undef CL_JIT_LEVEL_KIND_MEMBER
#undef CL_JIT_IR_LEVEL_MEMBERS_SemanticCoreMachineTransition
#undef CL_JIT_IR_LEVEL_MEMBERS_CoreMachineTransition
#undef CL_JIT_IR_LEVEL_MEMBERS_CoreTransition
#undef CL_JIT_IR_LEVEL_MEMBERS_CoreMachine
#undef CL_JIT_IR_LEVEL_MEMBERS_SemanticCore
#undef CL_JIT_IR_LEVEL_MEMBERS_Transition
#undef CL_JIT_IR_LEVEL_MEMBERS_Machine
#undef CL_JIT_IR_LEVEL_MEMBERS_Core
#undef CL_JIT_IR_LEVEL_MEMBERS_Semantic
#undef CL_JIT_IR_LEVEL_MEMBERS
#undef CL_JIT_LEVEL_KIND_JOIN
#undef CL_JIT_LEVEL_KIND_JOIN_INNER
    // clang-format on

    constexpr InstructionOrdinal instruction_ordinal(InstructionKind kind)
    {
        return static_cast<InstructionOrdinal>(static_cast<uint16_t>(kind) &
                                               InstructionOrdinalMask);
    }

    constexpr bool is_block_parameter_kind(InstructionKind kind)
    {
        return kind == InstructionKind::Parameter ||
               kind == InstructionKind::ParameterF64 ||
               kind == InstructionKind::ParameterPointer;
    }

    constexpr ResultClass instruction_result_class(InstructionKind kind)
    {
        return static_cast<ResultClass>(
            (static_cast<uint16_t>(kind) & InstructionResultClassMask) >>
            InstructionResultClassShift);
    }

    constexpr ResultClass
    instruction_result_class(TransitionInstructionKind kind)
    {
        return static_cast<ResultClass>(
            (static_cast<uint16_t>(kind) & InstructionResultClassMask) >>
            InstructionResultClassShift);
    }

    constexpr ValueRepresentation
    instruction_value_representation(InstructionKind kind)
    {
        return static_cast<ValueRepresentation>(
            (static_cast<uint16_t>(kind) & InstructionRepresentationMask) >>
            InstructionRepresentationShift);
    }

    constexpr bool
    instruction_kind_has_valid_result_encoding(InstructionKind kind)
    {
        ResultClass result_class = instruction_result_class(kind);
        ValueRepresentation representation =
            instruction_value_representation(kind);
        if(result_class == ResultClass::ProgramValue)
        {
            return representation == ValueRepresentation::TaggedValue ||
                   representation == ValueRepresentation::F64 ||
                   representation == ValueRepresentation::Pointer;
        }
        return (result_class == ResultClass::None ||
                result_class == ResultClass::Snapshot) &&
               representation == ValueRepresentation::None;
    }

    constexpr bool is_valid_instruction_kind(InstructionKind kind)
    {
        switch(kind)
        {
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    case InstructionKind::name:                                                \
        return true;
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
        }
        return false;
    }

    struct InstructionKindMetadata
    {
        static constexpr uint32_t NoSideExitArguments = UINT32_MAX;

        IRLevelMask allowed_ir_levels;
        EffectProfile must_effects;
        EffectProfile may_effects;
        uint32_t side_exit_argument_start;
        uint8_t fixed_operand_count;
        uint8_t attribute_count;
        uint8_t inline_slot_count;
        bool has_variadic_operands;
        bool operands_are_indirect;
    };

    const InstructionKindMetadata &
    instruction_kind_metadata(InstructionKind kind);

    constexpr bool ir_levels_include(IRLevelMask levels, IRLevelMask level)
    {
        uint8_t level_bits = static_cast<uint8_t>(level);
        return (static_cast<uint8_t>(levels) & level_bits) == level_bits;
    }

    inline bool instruction_kind_is_allowed_at(InstructionKind kind,
                                               IRLevelMask level)
    {
        IRLevelMask allowed = instruction_kind_metadata(kind).allowed_ir_levels;
        return ir_levels_include(allowed, level);
    }

    inline bool instruction_kind_is_allowed_at(InstructionKind kind,
                                               IRLevel level)
    {
        return instruction_kind_is_allowed_at(kind, ir_level_mask(level));
    }

    namespace instruction_detail
    {
        template <typename LevelKind, IRLevelMask Level,
                  typename ConcreteInstruction>
        consteval LevelKind instruction_kind_for_level()
        {
            static_assert(
                ir_levels_include(ConcreteInstruction::AllowedIRLevels, Level));
            return static_cast<LevelKind>(ConcreteInstruction::Kind);
        }
    }  // namespace instruction_detail

    inline SemanticInstructionKind
    semantic_instruction_kind(InstructionKind kind)
    {
        assert(instruction_kind_is_allowed_at(kind, IRLevelMask::Semantic));
        return static_cast<SemanticInstructionKind>(kind);
    }

    template <typename ConcreteInstruction>
    consteval SemanticInstructionKind semantic_instruction_kind()
    {
        return instruction_detail::instruction_kind_for_level<
            SemanticInstructionKind, IRLevelMask::Semantic,
            ConcreteInstruction>();
    }

    constexpr InstructionKind instruction_kind(SemanticInstructionKind kind)
    {
        return static_cast<InstructionKind>(kind);
    }

    inline CoreInstructionKind core_instruction_kind(InstructionKind kind)
    {
        assert(instruction_kind_is_allowed_at(kind, IRLevelMask::Core));
        return static_cast<CoreInstructionKind>(kind);
    }

    template <typename ConcreteInstruction>
    consteval CoreInstructionKind core_instruction_kind()
    {
        return instruction_detail::instruction_kind_for_level<
            CoreInstructionKind, IRLevelMask::Core, ConcreteInstruction>();
    }

    constexpr InstructionKind instruction_kind(CoreInstructionKind kind)
    {
        return static_cast<InstructionKind>(kind);
    }

    inline MachineInstructionKind machine_instruction_kind(InstructionKind kind)
    {
        assert(instruction_kind_is_allowed_at(kind, IRLevelMask::Machine));
        return static_cast<MachineInstructionKind>(kind);
    }

    template <typename ConcreteInstruction>
    consteval MachineInstructionKind machine_instruction_kind()
    {
        return instruction_detail::instruction_kind_for_level<
            MachineInstructionKind, IRLevelMask::Machine,
            ConcreteInstruction>();
    }

    constexpr InstructionKind instruction_kind(MachineInstructionKind kind)
    {
        return static_cast<InstructionKind>(kind);
    }

    inline TransitionInstructionKind
    transition_instruction_kind(InstructionKind kind)
    {
        assert(instruction_kind_is_allowed_at(kind, IRLevelMask::Transition));
        return static_cast<TransitionInstructionKind>(kind);
    }

    template <typename ConcreteInstruction>
    consteval TransitionInstructionKind transition_instruction_kind()
    {
        return instruction_detail::instruction_kind_for_level<
            TransitionInstructionKind, IRLevelMask::Transition,
            ConcreteInstruction>();
    }

    class alignas(16) InstructionEntry
    {
    public:
        using Slot = uint32_t;

        static constexpr size_t InlineSlotCount = 3;
        static constexpr uint16_t IndirectOperandsBit = uint16_t{1} << 15;
        static constexpr uint16_t OperandCountMask = IndirectOperandsBit - 1;
        static constexpr uint16_t PoisonedStorageTag = UINT16_MAX;

        bool is_poisoned() const { return kind_ == PoisonedStorageTag; }

        InstructionKind kind() const
        {
            if(is_poisoned())
            {
                fatal_poisoned_access();
            }
            InstructionKind result = static_cast<InstructionKind>(kind_);
            assert(is_valid_instruction_kind(result));
            return result;
        }

        uint16_t operand_count() const
        {
            return operand_storage_ & OperandCountMask;
        }

        bool operands_are_indirect() const
        {
            return (operand_storage_ & IndirectOperandsBit) != 0;
        }

        Slot slot(size_t index) const
        {
            assert(index < InlineSlotCount);
            return slots_[index];
        }

    private:
        friend class CompilationStorage;
        friend class Instruction;

        InstructionEntry(InstructionKind kind, uint16_t operand_count,
                         bool indirect_operands,
                         std::span<const Slot> inline_slots)
            : kind_(static_cast<uint16_t>(kind)),
              operand_storage_(operand_count |
                               (indirect_operands ? IndirectOperandsBit : 0))
        {
            assert(operand_count <= OperandCountMask);
            assert(inline_slots.size() <= InlineSlotCount);
            for(size_t index = 0; index < inline_slots.size(); ++index)
            {
                slots_[index] = inline_slots[index];
            }
            for(size_t index = inline_slots.size(); index < InlineSlotCount;
                ++index)
            {
                slots_[index] = 0;
            }
        }

        template <size_t N>
        InstructionEntry(InstructionKind kind, uint16_t operand_count,
                         bool indirect_operands,
                         const std::array<Slot, N> &inline_slots)
            : InstructionEntry(kind, operand_count, indirect_operands,
                               std::span<const Slot>(inline_slots))
        {
        }
        [[noreturn]] static void fatal_poisoned_access();

        void poison()
        {
            assert(!is_poisoned());
            kind_ = PoisonedStorageTag;
            operand_storage_ = PoisonedStorageTag;
            for(Slot &slot: slots_)
            {
                slot = UINT32_MAX;
            }
        }

        Slot slots_[InlineSlotCount];
        uint16_t kind_;
        uint16_t operand_storage_;
    };

    static_assert(sizeof(InstructionEntry) == 16);
    static_assert(alignof(InstructionEntry) == 16);
    static_assert(std::is_standard_layout_v<InstructionEntry>);
    static_assert(std::is_trivially_destructible_v<InstructionEntry>);
    static_assert(static_cast<uint16_t>(InstructionOrdinal::Count) <=
                  InstructionOrdinalMask + 1);
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    static_assert(                                                             \
        instruction_kind_has_valid_result_encoding(InstructionKind::name));
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
    static_assert(!is_valid_instruction_kind(
        static_cast<InstructionKind>(InstructionEntry::PoisonedStorageTag)));

    class Instruction
    {
    public:
        using Slot = InstructionEntry::Slot;

        static constexpr size_t InlineSlotCount =
            InstructionEntry::InlineSlotCount;
        static constexpr size_t IndirectOperandSlot = InlineSlotCount - 1;
        static constexpr uint16_t IndirectOperandsBit =
            InstructionEntry::IndirectOperandsBit;
        static constexpr uint16_t OperandCountMask =
            InstructionEntry::OperandCountMask;
        static constexpr uint16_t PoisonedStorageTag =
            InstructionEntry::PoisonedStorageTag;

        InstructionId id() const { return id_; }
        const CompilationStorage *storage() const { return storage_; }
        bool is_poisoned() const;
        InstructionKind kind() const;

        template <typename ConcreteInstruction> ConcreteInstruction as() const
        {
            assert(kind() == ConcreteInstruction::Kind);
            return ConcreteInstruction(storage_, id_);
        }

        ResultClass result_class() const
        {
            return instruction_result_class(kind());
        }

        ValueRepresentation value_representation() const
        {
            assert(result_class() == ResultClass::ProgramValue);
            return instruction_value_representation(kind());
        }

        bool is_block_terminator() const
        {
            return has_effects(instruction_kind_metadata(kind()).must_effects,
                               EffectProfile::TerminateBlock);
        }

        uint32_t side_exit_argument_start() const
        {
            return instruction_kind_metadata(kind()).side_exit_argument_start;
        }

        uint16_t operand_count() const;
        bool operands_are_indirect() const;
        Slot slot(size_t index) const;
        Slot operand_word(size_t index) const;

        friend bool operator==(Instruction, Instruction) = default;

    protected:
        friend class CompilationStorage;

        Instruction(const CompilationStorage *storage, InstructionId id)
            : storage_(storage), id_(id)
        {
            assert(storage != nullptr);
        }

        static InstructionEntry
        make_instruction_entry(InstructionKind kind, uint16_t operand_count,
                               bool indirect_operands,
                               std::span<const Slot> inline_slots)
        {
            return InstructionEntry(kind, operand_count, indirect_operands,
                                    inline_slots);
        }

        template <size_t N>
        static InstructionEntry
        make_instruction_entry(InstructionKind kind, uint16_t operand_count,
                               bool indirect_operands,
                               const std::array<Slot, N> &inline_slots)
        {
            return InstructionEntry(kind, operand_count, indirect_operands,
                                    inline_slots);
        }

        template <bool Indirect> Slot operand_word_at(size_t index) const
        {
            assert(index < operand_count());
            if constexpr(Indirect)
            {
                return operand_word(index);
            }
            return slot(index);
        }

        static consteval bool inline_storage_is_aligned(size_t index,
                                                        size_t storage_size)
        {
            return (offsetof(InstructionEntry, slots_) + index * sizeof(Slot)) %
                       storage_size ==
                   0;
        }

        template <size_t Index, typename Storage>
        const Slot *inline_words_at() const
        {
            static_assert(Index + sizeof(Storage) / sizeof(Slot) <=
                          InlineSlotCount);
            static_assert(inline_storage_is_aligned(Index, sizeof(Storage)));
            return entry().slots_ + Index;
        }

    private:
        const InstructionEntry &entry() const;

        const CompilationStorage *storage_;
        InstructionId id_;
    };

    static_assert(sizeof(Instruction) == 16);

    class ProgramValueRef
    {
    public:
        explicit ProgramValueRef(Instruction instruction)
            : instruction_(checked_instruction_id(instruction))
        {
        }

        InstructionId instruction_id() const { return instruction_; }

        friend bool operator==(ProgramValueRef, ProgramValueRef) = default;

    private:
        template <OperandClass, ValueRepresentation>
        friend auto decode_instruction_operand(uint32_t);
        template <ValueRepresentation> friend class RepresentedValueRef;
        friend class LiveRangeOrigin;
        friend class ProgramValueRefRange;

        static InstructionId checked_instruction_id(Instruction instruction)
        {
            assert(instruction.result_class() == ResultClass::ProgramValue);
            return instruction.id();
        }

        explicit ProgramValueRef(InstructionId instruction)
            : instruction_(instruction)
        {
        }

        InstructionId instruction_;
    };

    class SnapshotRef
    {
    public:
        explicit SnapshotRef(Instruction instruction)
            : instruction_(checked_instruction_id(instruction))
        {
        }

        InstructionId instruction_id() const { return instruction_; }

        friend bool operator==(SnapshotRef, SnapshotRef) = default;

    private:
        template <OperandClass, ValueRepresentation>
        friend auto decode_instruction_operand(uint32_t);

        static InstructionId checked_instruction_id(Instruction instruction)
        {
            assert(instruction.result_class() == ResultClass::Snapshot);
            return instruction.id();
        }

        explicit SnapshotRef(InstructionId instruction)
            : instruction_(instruction)
        {
        }

        InstructionId instruction_;
    };

    template <ValueRepresentation Representation> class RepresentedValueRef
    {
    public:
        explicit RepresentedValueRef(Instruction instruction)
            : reference_(instruction)
        {
            assert(instruction.value_representation() == Representation);
        }

        InstructionId instruction_id() const
        {
            return reference_.instruction_id();
        }
        operator ProgramValueRef() const { return reference_; }

        friend bool operator==(RepresentedValueRef,
                               RepresentedValueRef) = default;

    private:
        template <OperandClass, ValueRepresentation>
        friend auto decode_instruction_operand(uint32_t);

        explicit RepresentedValueRef(InstructionId instruction)
            : reference_(instruction)
        {
        }

        ProgramValueRef reference_;
    };

    using TaggedValueRef =
        RepresentedValueRef<ValueRepresentation::TaggedValue>;
    using F64Ref = RepresentedValueRef<ValueRepresentation::F64>;
    using PointerRef = RepresentedValueRef<ValueRepresentation::Pointer>;

    static_assert(sizeof(ProgramValueRef) == sizeof(uint32_t));
    static_assert(sizeof(SnapshotRef) == sizeof(uint32_t));
    static_assert(sizeof(TaggedValueRef) == sizeof(uint32_t));
    static_assert(sizeof(F64Ref) == sizeof(uint32_t));
    static_assert(sizeof(PointerRef) == sizeof(uint32_t));

    template <OperandClass Class, ValueRepresentation Representation>
    auto decode_instruction_operand(uint32_t word)
    {
        InstructionId instruction(static_cast<uint32_t>(word));
        if constexpr(Class == OperandClass::Snapshot)
        {
            static_assert(Representation == ValueRepresentation::None);
            return SnapshotRef(instruction);
        }
        else if constexpr(Representation == ValueRepresentation::TaggedValue)
        {
            return TaggedValueRef(instruction);
        }
        else if constexpr(Representation == ValueRepresentation::F64)
        {
            return F64Ref(instruction);
        }
        else
        {
            static_assert(Representation == ValueRepresentation::Pointer);
            return PointerRef(instruction);
        }
    }

    template <ValueRepresentation Representation> class RepresentedValueRefRange
    {
    public:
        RepresentedValueRefRange(Instruction instruction, uint32_t offset,
                                 uint32_t size)
            : instruction_(instruction), offset_(offset), size_(size)
        {
            assert(offset <= instruction.operand_count());
            assert(size <= instruction.operand_count() - offset);
        }

        size_t size() const { return size_; }
        bool empty() const { return size_ == 0; }

        auto operator[](size_t index) const
        {
            assert(index < size_);
            return decode_instruction_operand<OperandClass::ProgramValue,
                                              Representation>(
                instruction_.operand_word(offset_ + index));
        }

    private:
        Instruction instruction_;
        uint32_t offset_;
        uint32_t size_;
    };

    class ProgramValueRefRange
    {
    public:
        ProgramValueRefRange(Instruction instruction, uint32_t offset,
                             uint32_t size)
            : instruction_(instruction), offset_(offset), size_(size)
        {
            assert(offset <= instruction.operand_count());
            assert(size <= instruction.operand_count() - offset);
        }

        size_t size() const { return size_; }
        bool empty() const { return size_ == 0; }

        ProgramValueRef operator[](size_t index) const
        {
            assert(index < size_);
            return ProgramValueRef(
                InstructionId(instruction_.operand_word(offset_ + index)));
        }

    private:
        Instruction instruction_;
        uint32_t offset_;
        uint32_t size_;
    };

    using BytecodePC = uint32_t;

    using InstructionAttributeStorage_Shape = uint64_t;
    using InstructionAttributeStorage_ValidityCell = uint64_t;
    using InstructionAttributeStorage_ShapeKey = uint64_t;
    using InstructionAttributeStorage_ValueConstant = uint64_t;
    using InstructionAttributeStorage_BytecodePC = uint32_t;
    using InstructionAttributeStorage_SideExitId = uint32_t;
    using InstructionAttributeStorage_BlockEdge = uint32_t;

    static_assert(sizeof(uintptr_t) == sizeof(uint64_t));
    static_assert(sizeof(ShapeKey) ==
                  sizeof(InstructionAttributeStorage_ShapeKey));
    static_assert(sizeof(Value) ==
                  sizeof(InstructionAttributeStorage_ValueConstant));

    template <typename Storage>
    Storage decode_instruction_attribute_storage(const uint32_t *words)
    {
        static_assert(std::is_trivially_copyable_v<Storage>);
        static_assert(sizeof(Storage) % sizeof(uint32_t) == 0);
        Storage result;
        std::memcpy(&result, words, sizeof(result));
        return result;
    }

    template <typename Storage>
    void encode_instruction_attribute_storage(uint32_t *words, Storage value)
    {
        static_assert(std::is_trivially_copyable_v<Storage>);
        static_assert(sizeof(Storage) % sizeof(uint32_t) == 0);
        std::memcpy(words, &value, sizeof(value));
    }

    inline Shape *decode_instruction_attribute_Shape(const CompilationStorage *,
                                                     const uint32_t *words)
    {
        uint64_t value = decode_instruction_attribute_storage<uint64_t>(words);
        return reinterpret_cast<Shape *>(static_cast<uintptr_t>(value));
    }

    inline ValidityCell *
    decode_instruction_attribute_ValidityCell(const CompilationStorage *,
                                              const uint32_t *words)
    {
        uint64_t value = decode_instruction_attribute_storage<uint64_t>(words);
        return reinterpret_cast<ValidityCell *>(static_cast<uintptr_t>(value));
    }

    inline ShapeKey
    decode_instruction_attribute_ShapeKey(const CompilationStorage *,
                                          const uint32_t *words)
    {
        return decode_instruction_attribute_storage<ShapeKey>(words);
    }

    inline Value
    decode_instruction_attribute_ValueConstant(const CompilationStorage *,
                                               const uint32_t *words)
    {
        return decode_instruction_attribute_storage<Value>(words);
    }

    inline BytecodePC
    decode_instruction_attribute_BytecodePC(const CompilationStorage *,
                                            const uint32_t *words)
    {
        return decode_instruction_attribute_storage<BytecodePC>(words);
    }

    inline SideExitId
    decode_instruction_attribute_SideExitId(const CompilationStorage *,
                                            const uint32_t *words)
    {
        return SideExitId(*words);
    }

    BlockEdge *
    decode_instruction_attribute_BlockEdge(const CompilationStorage *storage,
                                           const uint32_t *words);

    inline uint32_t encode_instruction_operand(TaggedValueRef reference)
    {
        return reference.instruction_id().value();
    }

    inline uint32_t encode_instruction_operand(ProgramValueRef reference)
    {
        return reference.instruction_id().value();
    }

    inline uint32_t encode_instruction_operand(F64Ref reference)
    {
        return reference.instruction_id().value();
    }

    inline uint32_t encode_instruction_operand(SnapshotRef reference)
    {
        return reference.instruction_id().value();
    }

    inline void encode_instruction_attribute_Shape(uint32_t *words,
                                                   Shape *shape)
    {
        assert(shape != nullptr);
        encode_instruction_attribute_storage(
            words, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(shape)));
    }

    inline void
    encode_instruction_attribute_ValidityCell(uint32_t *words,
                                              ValidityCell *validity)
    {
        assert(validity != nullptr);
        encode_instruction_attribute_storage(
            words,
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(validity)));
    }

    inline void encode_instruction_attribute_ShapeKey(uint32_t *words,
                                                      ShapeKey shape_key)
    {
        encode_instruction_attribute_storage(words, shape_key);
    }

    inline void encode_instruction_attribute_ValueConstant(uint32_t *words,
                                                           Value value)
    {
        encode_instruction_attribute_storage(words, value);
    }

    inline void encode_instruction_attribute_BytecodePC(uint32_t *words,
                                                        BytecodePC pc)
    {
        encode_instruction_attribute_storage(words, pc);
    }

    inline void encode_instruction_attribute_SideExitId(uint32_t *words,
                                                        SideExitId side_exit)
    {
        *words = side_exit.value();
    }

    void encode_instruction_attribute_BlockEdge(uint32_t *words,
                                                BlockEdge *edge);

    struct InstructionConstructorEnd
    {
    };

    // Representative simplified expansion of the schema-generated classes:
    //
    // class ShapeGuardInstruction final : public Instruction
    // {
    // public:
    //     static constexpr InstructionKind Kind =
    //         InstructionKind::ShapeGuard;
    //     static constexpr ResultClass Result = ResultClass::ProgramValue;
    //     static constexpr ValueRepresentation Representation =
    //         ValueRepresentation::TaggedValue;
    //     static constexpr EffectProfile MustEffects = EffectProfile::None;
    //     static constexpr EffectProfile MayEffects =
    //         EffectProfile::SideExit;
    //     static constexpr IRLevelMask AllowedIRLevels = IRLevelMask::Core;
    //     static constexpr bool IsVariadic = false;
    //     static constexpr bool OperandsAreIndirect = false;
    //
    //     TaggedValueRef object() const;
    //     SnapshotRef snapshot() const;
    //     Shape *expected_shape() const;
    //
    // private:
    //     friend class CompilationStorage;
    //     ShapeGuardInstruction(InstructionId id, TaggedValueRef object,
    //                           SnapshotRef snapshot, Shape *expected_shape);
    // };
    //
    // Classes with indirect operands additionally expose
    // n_indirect_slots_for(...), and their private constructor receives the
    // storage-allocated indirect span after the ID. The macros below generate
    // these declarations, their slot encoders, and the accessor definitions
    // from instruction.def.

    // clang-format off
#define CL_JIT_JOIN_INNER(first, second) first##second
#define CL_JIT_JOIN(first, second) CL_JIT_JOIN_INNER(first, second)
#define CL_JIT_OPERAND_TYPE_ProgramValue_TaggedValue TaggedValueRef
#define CL_JIT_OPERAND_TYPE_ProgramValue_F64 F64Ref
#define CL_JIT_OPERAND_TYPE_ProgramValue_Pointer PointerRef
#define CL_JIT_OPERAND_TYPE_Snapshot_None SnapshotRef
#define CL_JIT_OPERAND_TYPE_INNER(operand_class, representation)               \
    CL_JIT_OPERAND_TYPE_##operand_class##_##representation
#define CL_JIT_OPERAND_TYPE(operand_class, representation)                     \
    CL_JIT_OPERAND_TYPE_INNER(operand_class, representation)
#define CL_JIT_ATTRIBUTE_TYPE_Shape Shape *
#define CL_JIT_ATTRIBUTE_TYPE_ValidityCell ValidityCell *
#define CL_JIT_ATTRIBUTE_TYPE_ShapeKey ShapeKey
#define CL_JIT_ATTRIBUTE_TYPE_ValueConstant Value
#define CL_JIT_ATTRIBUTE_TYPE_BytecodePC BytecodePC
#define CL_JIT_ATTRIBUTE_TYPE_SideExitId SideExitId
#define CL_JIT_ATTRIBUTE_TYPE_BlockEdge BlockEdge *
#define CL_JIT_ATTRIBUTE_TYPE(attribute_class)                                 \
    CL_JIT_JOIN(CL_JIT_ATTRIBUTE_TYPE_, attribute_class)
#define CL_JIT_DECLARE_OPERAND_INDEX(name, operand_class, representation) name,
#define CL_JIT_DECLARE_VARIADIC_INDEX(name, operand_class, representation) name,
#define CL_JIT_DECLARE_PROGRAM_VALUES_INDEX(name, role) name,
#define CL_JIT_IR_LEVELS(set) IRLevelMask::set
#define CL_JIT_RESULT(result_class, representation)                            \
    InstructionResultInfo                                                      \
    {                                                                          \
        ResultClass::result_class, ValueRepresentation::representation         \
    }
#define CL_JIT_EFFECT_BOUNDS(must_effects, may_effects)                        \
    InstructionEffectBounds                                                    \
    {                                                                          \
        EffectProfile::must_effects, EffectProfile::may_effects                \
    }
#define CL_JIT_EFFECT_BOUNDS_MAY_TWO(must_effects, may_first, may_second)      \
    InstructionEffectBounds                                                    \
    {                                                                          \
        EffectProfile::must_effects,                                            \
            EffectProfile::may_first | EffectProfile::may_second               \
    }
#define CL_JIT_EXACT_EFFECTS_TWO(first, second)                                \
    InstructionEffectBounds                                                    \
    {                                                                          \
        EffectProfile::first | EffectProfile::second,                          \
            EffectProfile::first | EffectProfile::second                       \
    }
#define CL_JIT_EXACT_EFFECTS_THREE(first, second, third)                       \
    InstructionEffectBounds                                                    \
    {                                                                          \
        EffectProfile::first | EffectProfile::second | EffectProfile::third,   \
            EffectProfile::first | EffectProfile::second |                     \
                EffectProfile::third                                           \
    }
#define CL_JIT_COUNT_FIXED_OPERAND(...) +1
#define CL_JIT_COUNT_NO_OPERAND(...) +0
#define CL_JIT_HAS_NO_VARIADIC(...) || false
#define CL_JIT_HAS_VARIADIC(...) || true
#define CL_JIT_DECLARE_FIXED_PARAMETER(name, operand_class, representation)    \
    CL_JIT_OPERAND_TYPE(operand_class, representation) name,
#define CL_JIT_DECLARE_VARIADIC_PARAMETER(name, operand_class, representation) \
    std::span<const CL_JIT_OPERAND_TYPE(operand_class, representation)> name,
#define CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER(name, role)                    \
    std::span<const ProgramValueRef> name,
#define CL_JIT_DECLARE_ATTRIBUTE_PARAMETER(name, attribute_class)              \
    CL_JIT_ATTRIBUTE_TYPE(attribute_class) name,
#define CL_JIT_PASS_ARGUMENT(name, ...) name,
#define CL_JIT_IGNORE_ARGUMENT(name, ...) (void)name;
#define CL_JIT_COUNT_INDIRECT_FIXED(name, ...) (void)name;
#define CL_JIT_COUNT_INDIRECT_VARIADIC(name, ...) n_slots += name.size();
#define CL_JIT_COUNT_INDIRECT_PROGRAM_VALUES(name, role) n_slots += name.size();
#define CL_JIT_COUNT_LOGICAL_FIXED(name, ...) (void)name;
#define CL_JIT_COUNT_LOGICAL_VARIADIC(name, ...) n_operands += name.size();
#define CL_JIT_COUNT_LOGICAL_PROGRAM_VALUES(name, role) n_operands += name.size();
#define CL_JIT_SKIP_INLINE(...)
#define CL_JIT_COUNT_ATTRIBUTE_WORDS(name, attribute_class)                    \
    +sizeof(InstructionAttributeStorage_##attribute_class) / sizeof(Slot)
#define CL_JIT_CHECK_ATTRIBUTE_ALIGNMENT(name, attribute_class)                \
    aligned = aligned &&                                                       \
              inline_storage_is_aligned(                                       \
                  offset, sizeof(InstructionAttributeStorage_##attribute_class)); \
    offset +=                                                                  \
        sizeof(InstructionAttributeStorage_##attribute_class) / sizeof(Slot);
#define CL_JIT_FIND_ATTRIBUTE_SLOT(name, attribute_class)                      \
    if(current == target)                                                      \
    {                                                                          \
        return offset;                                                         \
    }                                                                          \
    ++current;                                                                 \
    offset +=                                                                  \
        sizeof(InstructionAttributeStorage_##attribute_class) / sizeof(Slot);
#define CL_JIT_WRITE_FIXED_INLINE(name, ...)                                   \
    inline_slots[index++] = encode_instruction_operand(name);
#define CL_JIT_WRITE_ATTRIBUTE_INLINE(name, attribute_class)                   \
    encode_instruction_attribute_##attribute_class(inline_slots.data() +      \
                                                        index,                 \
                                                    name);                     \
    index +=                                                                   \
        sizeof(InstructionAttributeStorage_##attribute_class) / sizeof(Slot);
#define CL_JIT_WRITE_INDIRECT_FIXED(name, ...)                                 \
    indirect_slots[index++] = encode_instruction_operand(name);
#define CL_JIT_WRITE_INDIRECT_VARIADIC(name, ...)                              \
    for(const auto &operand: name)                                             \
    {                                                                          \
        indirect_slots[index++] = encode_instruction_operand(operand);         \
    }
#define CL_JIT_WRITE_INDIRECT_PROGRAM_VALUES(name, role)                       \
    for(ProgramValueRef value: name)                                           \
    {                                                                          \
        indirect_slots[index++] = encode_instruction_operand(value);           \
    }
#define CL_JIT_DECLARE_ATTRIBUTE_INDEX(name, attribute_class) name,
#define CL_JIT_PRIVATE private:
#define CL_JIT_DECLARE_FIXED_ACCESSOR(name, operand_class, representation)     \
    static constexpr uint32_t name##_operand_index =                           \
        static_cast<uint32_t>(OperandIndex::name);                             \
    auto name() const                                                          \
    {                                                                          \
        constexpr size_t index = static_cast<size_t>(OperandIndex::name);      \
        return decode_instruction_operand<                                     \
            OperandClass::operand_class, ValueRepresentation::representation>( \
            operand_word_at<OperandsAreIndirect>(index));                      \
    }
#define CL_JIT_DECLARE_VARIADIC_ACCESSOR(name, operand_class, representation)  \
    static constexpr uint32_t name##_operand_index =                           \
        static_cast<uint32_t>(OperandIndex::name);                             \
    auto name() const                                                          \
    {                                                                          \
        static_assert(OperandClass::operand_class ==                           \
                      OperandClass::ProgramValue);                             \
        constexpr size_t index = static_cast<size_t>(OperandIndex::name);      \
        return RepresentedValueRefRange<ValueRepresentation::representation>(  \
            *this, static_cast<uint32_t>(index), operand_count() - index);      \
    }
#define CL_JIT_DECLARE_PROGRAM_VALUES_ACCESSOR(name, role)                     \
    static constexpr uint32_t name##_operand_index =                           \
        static_cast<uint32_t>(OperandIndex::name);                             \
    ProgramValueRefRange name() const                                          \
    {                                                                          \
        constexpr size_t index = static_cast<size_t>(OperandIndex::name);      \
        return ProgramValueRefRange(                                           \
            *this, static_cast<uint32_t>(index), operand_count() - index);      \
    }
#define CL_JIT_DECLARE_ATTRIBUTE_ACCESSOR(name, attribute_class)               \
    auto name() const                                                          \
    {                                                                          \
        constexpr size_t index = attribute_slot(AttributeIndex::name);         \
        return decode_instruction_attribute_##attribute_class(                 \
            storage(),                                                        \
            inline_words_at<index,                                             \
                            InstructionAttributeStorage_##attribute_class>());  \
    }
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    class name##Instruction final : public Instruction                         \
    {                                                                          \
    private:                                                                   \
        enum class OperandIndex : size_t                                       \
        {                                                                      \
            operands(CL_JIT_DECLARE_OPERAND_INDEX,                             \
                     CL_JIT_DECLARE_VARIADIC_INDEX,                            \
                     CL_JIT_DECLARE_PROGRAM_VALUES_INDEX) Count,               \
        };                                                                     \
        enum class AttributeIndex : size_t                                     \
        {                                                                      \
            attributes(CL_JIT_DECLARE_ATTRIBUTE_INDEX) Count,                  \
        };                                                                     \
        static constexpr size_t FixedOperandCount =                            \
            0 operands(CL_JIT_COUNT_FIXED_OPERAND, CL_JIT_COUNT_NO_OPERAND,    \
                       CL_JIT_COUNT_NO_OPERAND);                               \
        static constexpr size_t AttributeCount =                               \
            static_cast<size_t>(AttributeIndex::Count);                        \
        static constexpr size_t AttributeWordCount =                           \
            0 attributes(CL_JIT_COUNT_ATTRIBUTE_WORDS);                        \
        static constexpr bool DirectAttributesAligned = [] {                   \
            size_t offset = FixedOperandCount;                                 \
            bool aligned = true;                                               \
            attributes(CL_JIT_CHECK_ATTRIBUTE_ALIGNMENT)                      \
            (void)offset;                                                      \
            return aligned;                                                    \
        }();                                                                   \
        static constexpr bool IndirectAttributesAligned = [] {                 \
            size_t offset = 0;                                                 \
            bool aligned = true;                                               \
            attributes(CL_JIT_CHECK_ATTRIBUTE_ALIGNMENT)                      \
            (void)offset;                                                      \
            return aligned;                                                    \
        }();                                                                   \
                                                                               \
    public:                                                                    \
        static constexpr InstructionKind Kind = InstructionKind::name;         \
        static constexpr ResultClass Result = (result).result_class;           \
        static constexpr ValueRepresentation Representation =                  \
            (result).representation;                                           \
        static constexpr EffectProfile MustEffects = (effects).must_effects;   \
        static constexpr EffectProfile MayEffects = (effects).may_effects;     \
        static constexpr IRLevelMask AllowedIRLevels = ir_levels;              \
        static constexpr bool IsVariadic = false operands(                     \
            CL_JIT_HAS_NO_VARIADIC, CL_JIT_HAS_VARIADIC, CL_JIT_HAS_VARIADIC); \
        static constexpr bool OperandsAreIndirect =                            \
            IsVariadic ||                                                      \
            FixedOperandCount + AttributeWordCount > InlineSlotCount ||        \
            !DirectAttributesAligned;                                          \
                                                                               \
        template <bool Indirect = OperandsAreIndirect>                         \
        requires(Indirect)                                                     \
        static size_t n_indirect_slots_for(                                    \
            operands(CL_JIT_DECLARE_FIXED_PARAMETER,                           \
                     CL_JIT_DECLARE_VARIADIC_PARAMETER,                        \
                     CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER)                  \
                attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)                 \
                    InstructionConstructorEnd = {})                            \
        {                                                                      \
            size_t n_operands = FixedOperandCount;                             \
            operands(CL_JIT_COUNT_LOGICAL_FIXED,                              \
                     CL_JIT_COUNT_LOGICAL_VARIADIC,                           \
                     CL_JIT_COUNT_LOGICAL_PROGRAM_VALUES)                     \
            attributes(CL_JIT_IGNORE_ARGUMENT)                                \
            assert(n_operands <= OperandCountMask);                            \
            (void)n_operands;                                                  \
            size_t n_slots = FixedOperandCount;                                \
            operands(CL_JIT_COUNT_INDIRECT_FIXED,                              \
                     CL_JIT_COUNT_INDIRECT_VARIADIC,                           \
                     CL_JIT_COUNT_INDIRECT_PROGRAM_VALUES)                     \
            attributes(CL_JIT_IGNORE_ARGUMENT)                                \
            return n_slots;                                                    \
        }                                                                      \
                                                                               \
    operands(CL_JIT_DECLARE_FIXED_ACCESSOR, CL_JIT_DECLARE_VARIADIC_ACCESSOR,  \
             CL_JIT_DECLARE_PROGRAM_VALUES_ACCESSOR)                           \
                                                                               \
    CL_JIT_PRIVATE                                                             \
        static constexpr size_t AttributeBase =                                \
            OperandsAreIndirect ? 0 : FixedOperandCount;                       \
        static consteval size_t attribute_slot(AttributeIndex target_index)    \
        {                                                                      \
            size_t target = static_cast<size_t>(target_index);                 \
            size_t current = 0;                                                \
            size_t offset = AttributeBase;                                     \
            attributes(CL_JIT_FIND_ATTRIBUTE_SLOT)                            \
            (void)target;                                                      \
            (void)current;                                                     \
            assert(current == target);                                         \
            return offset;                                                     \
        }                                                                      \
        static constexpr size_t InlineSlotCountForKind =                       \
            OperandsAreIndirect ? InlineSlotCount                              \
                                : AttributeBase + AttributeWordCount;          \
        static_assert(InlineSlotCountForKind <= InlineSlotCount);              \
        static_assert(!OperandsAreIndirect ||                                  \
                      AttributeWordCount + 1 <= InlineSlotCount);              \
        static_assert(!OperandsAreIndirect || IndirectAttributesAligned);      \
                                                                               \
    public:                                                                    \
        attributes(CL_JIT_DECLARE_ATTRIBUTE_ACCESSOR)                          \
                                                                               \
    private:                                                                   \
        static uint16_t                                                        \
        operand_count_for(operands(CL_JIT_DECLARE_FIXED_PARAMETER,             \
                                   CL_JIT_DECLARE_VARIADIC_PARAMETER,          \
                                   CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER)    \
                              attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)   \
                                  InstructionConstructorEnd = {})              \
        {                                                                      \
            size_t n_operands = FixedOperandCount;                             \
            operands(CL_JIT_COUNT_LOGICAL_FIXED,                               \
                     CL_JIT_COUNT_LOGICAL_VARIADIC,                            \
                     CL_JIT_COUNT_LOGICAL_PROGRAM_VALUES)                      \
            attributes(CL_JIT_IGNORE_ARGUMENT)                                \
            assert(n_operands <= OperandCountMask);                            \
            return static_cast<uint16_t>(n_operands);                          \
        }                                                                      \
                                                                               \
        static std::array<Slot, FixedOperandCount + AttributeWordCount>        \
        fixed_inline_slots(operands(CL_JIT_DECLARE_FIXED_PARAMETER,            \
                                    CL_JIT_DECLARE_VARIADIC_PARAMETER,         \
                                    CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER)   \
                               attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)  \
                                   InstructionConstructorEnd = {})             \
        {                                                                      \
            std::array<Slot, FixedOperandCount + AttributeWordCount>           \
                inline_slots{};                                                \
            size_t index = 0;                                                  \
            operands(CL_JIT_WRITE_FIXED_INLINE, CL_JIT_SKIP_INLINE,            \
                     CL_JIT_SKIP_INLINE)                                       \
            attributes(CL_JIT_WRITE_ATTRIBUTE_INLINE)                         \
            (void)index;                                                       \
            assert(index == inline_slots.size());                              \
            return inline_slots;                                               \
        }                                                                      \
                                                                               \
        static void initialize_indirect_slots(                                 \
            std::span<Slot> indirect_slots,                                   \
            operands(CL_JIT_DECLARE_FIXED_PARAMETER,                           \
                     CL_JIT_DECLARE_VARIADIC_PARAMETER,                        \
                     CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER)                  \
                attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)                 \
                    InstructionConstructorEnd = {})                            \
        {                                                                      \
            size_t index = 0;                                                  \
            operands(CL_JIT_WRITE_INDIRECT_FIXED,                              \
                     CL_JIT_WRITE_INDIRECT_VARIADIC,                           \
                     CL_JIT_WRITE_INDIRECT_PROGRAM_VALUES)                     \
            attributes(CL_JIT_IGNORE_ARGUMENT)                                \
            (void)index;                                                       \
            assert(index == indirect_slots.size());                            \
        }                                                                      \
                                                                               \
        static std::array<Slot, InlineSlotCount> indirect_inline_slots(        \
            uint32_t indirect_offset,                                          \
            std::span<Slot> indirect_slots,                                   \
            operands(CL_JIT_DECLARE_FIXED_PARAMETER,                           \
                     CL_JIT_DECLARE_VARIADIC_PARAMETER,                        \
                     CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER)                  \
                attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)                 \
                    InstructionConstructorEnd = {})                            \
        {                                                                      \
            initialize_indirect_slots(                                        \
                indirect_slots,                                                \
                operands(CL_JIT_PASS_ARGUMENT, CL_JIT_PASS_ARGUMENT,           \
                         CL_JIT_PASS_ARGUMENT)                                 \
                    attributes(CL_JIT_PASS_ARGUMENT){});                       \
            std::array<Slot, InlineSlotCount> inline_slots{};                   \
            size_t index = 0;                                                  \
            attributes(CL_JIT_WRITE_ATTRIBUTE_INLINE)                         \
            (void)index;                                                       \
            assert(index == AttributeWordCount);                               \
            inline_slots[IndirectOperandSlot] = indirect_offset;               \
            return inline_slots;                                               \
        }                                                                      \
                                                                               \
        friend class CompilationStorage;                                       \
        friend class Instruction;                                              \
        name##Instruction(const CompilationStorage *storage, InstructionId id) \
            : Instruction(storage, id)                                         \
        {                                                                      \
        }                                                                      \
                                                                               \
        template <bool Indirect = OperandsAreIndirect>                         \
        requires(!Indirect)                                                    \
        static InstructionEntry make_entry(                                    \
            operands(CL_JIT_DECLARE_FIXED_PARAMETER,                           \
                     CL_JIT_DECLARE_VARIADIC_PARAMETER,                        \
                     CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER)                  \
                attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)                 \
                    InstructionConstructorEnd = {})                            \
        {                                                                      \
            return make_instruction_entry(                                    \
                Kind, static_cast<uint16_t>(FixedOperandCount), false,         \
                fixed_inline_slots(                                            \
                    operands(CL_JIT_PASS_ARGUMENT, CL_JIT_PASS_ARGUMENT,       \
                             CL_JIT_PASS_ARGUMENT)                             \
                        attributes(CL_JIT_PASS_ARGUMENT){}));                  \
        }                                                                      \
                                                                               \
        template <bool Indirect = OperandsAreIndirect>                         \
        requires(Indirect)                                                     \
        static InstructionEntry make_entry(                                    \
            uint32_t indirect_offset,                                          \
            std::span<Slot> indirect_slots,                                    \
            operands(CL_JIT_DECLARE_FIXED_PARAMETER,                           \
                     CL_JIT_DECLARE_VARIADIC_PARAMETER,                        \
                     CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER)                  \
                attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)                 \
                    InstructionConstructorEnd = {})                            \
        {                                                                      \
            return make_instruction_entry(                                    \
                Kind,                                                          \
                operand_count_for(                                             \
                    operands(CL_JIT_PASS_ARGUMENT, CL_JIT_PASS_ARGUMENT,       \
                             CL_JIT_PASS_ARGUMENT)                             \
                        attributes(CL_JIT_PASS_ARGUMENT){}),                   \
                true,                                                          \
                indirect_inline_slots(                                         \
                    indirect_offset, indirect_slots,                           \
                    operands(CL_JIT_PASS_ARGUMENT, CL_JIT_PASS_ARGUMENT,       \
                             CL_JIT_PASS_ARGUMENT)                             \
                        attributes(CL_JIT_PASS_ARGUMENT){}));                  \
        }                                                                      \
    };                                                                         \
    static_assert(sizeof(name##Instruction) == sizeof(Instruction));           \
    static_assert(std::is_base_of_v<Instruction, name##Instruction>);          \
    static_assert(std::is_trivially_destructible_v<name##Instruction>);        \
    static_assert(name##Instruction::Result ==                                 \
                  instruction_result_class(name##Instruction::Kind));          \
    static_assert(name##Instruction::Representation ==                         \
                  instruction_value_representation(name##Instruction::Kind));  \
    static_assert(name##Instruction::AllowedIRLevels != IRLevelMask::None);
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_EXACT_EFFECTS_THREE
#undef CL_JIT_EXACT_EFFECTS_TWO
#undef CL_JIT_EFFECT_BOUNDS_MAY_TWO
#undef CL_JIT_EFFECT_BOUNDS
#undef CL_JIT_RESULT
#undef CL_JIT_IR_LEVELS
#undef CL_JIT_DECLARE_ATTRIBUTE_ACCESSOR
#undef CL_JIT_DECLARE_PROGRAM_VALUES_ACCESSOR
#undef CL_JIT_DECLARE_VARIADIC_ACCESSOR
#undef CL_JIT_DECLARE_FIXED_ACCESSOR
#undef CL_JIT_PRIVATE
#undef CL_JIT_DECLARE_ATTRIBUTE_INDEX
#undef CL_JIT_HAS_VARIADIC
#undef CL_JIT_HAS_NO_VARIADIC
#undef CL_JIT_COUNT_NO_OPERAND
#undef CL_JIT_COUNT_FIXED_OPERAND
#undef CL_JIT_DECLARE_PROGRAM_VALUES_INDEX
#undef CL_JIT_DECLARE_VARIADIC_INDEX
#undef CL_JIT_DECLARE_OPERAND_INDEX
#undef CL_JIT_WRITE_INDIRECT_PROGRAM_VALUES
#undef CL_JIT_WRITE_INDIRECT_VARIADIC
#undef CL_JIT_WRITE_INDIRECT_FIXED
#undef CL_JIT_WRITE_ATTRIBUTE_INLINE
#undef CL_JIT_WRITE_FIXED_INLINE
#undef CL_JIT_FIND_ATTRIBUTE_SLOT
#undef CL_JIT_CHECK_ATTRIBUTE_ALIGNMENT
#undef CL_JIT_COUNT_ATTRIBUTE_WORDS
#undef CL_JIT_SKIP_INLINE
#undef CL_JIT_COUNT_LOGICAL_PROGRAM_VALUES
#undef CL_JIT_COUNT_LOGICAL_VARIADIC
#undef CL_JIT_COUNT_LOGICAL_FIXED
#undef CL_JIT_COUNT_INDIRECT_PROGRAM_VALUES
#undef CL_JIT_COUNT_INDIRECT_VARIADIC
#undef CL_JIT_COUNT_INDIRECT_FIXED
#undef CL_JIT_IGNORE_ARGUMENT
#undef CL_JIT_PASS_ARGUMENT
#undef CL_JIT_DECLARE_ATTRIBUTE_PARAMETER
#undef CL_JIT_DECLARE_PROGRAM_VALUES_PARAMETER
#undef CL_JIT_DECLARE_VARIADIC_PARAMETER
#undef CL_JIT_DECLARE_FIXED_PARAMETER
#undef CL_JIT_ATTRIBUTE_TYPE
#undef CL_JIT_ATTRIBUTE_TYPE_BlockEdge
#undef CL_JIT_ATTRIBUTE_TYPE_SideExitId
#undef CL_JIT_ATTRIBUTE_TYPE_BytecodePC
#undef CL_JIT_ATTRIBUTE_TYPE_ValueConstant
#undef CL_JIT_ATTRIBUTE_TYPE_ShapeKey
#undef CL_JIT_ATTRIBUTE_TYPE_ValidityCell
#undef CL_JIT_ATTRIBUTE_TYPE_Shape
#undef CL_JIT_OPERAND_TYPE
#undef CL_JIT_OPERAND_TYPE_INNER
#undef CL_JIT_OPERAND_TYPE_Snapshot_None
#undef CL_JIT_OPERAND_TYPE_ProgramValue_Pointer
#undef CL_JIT_OPERAND_TYPE_ProgramValue_F64
#undef CL_JIT_OPERAND_TYPE_ProgramValue_TaggedValue
#undef CL_JIT_JOIN
#undef CL_JIT_JOIN_INNER
    // clang-format on

// Preserve a compiler-visible, IR-specific switch while binding each case to
// the checked, read-only concrete instruction type named by that case.
// clang-format off
#define CL_JIT_LEVEL_INSTRUCTION_SWITCH(instruction, convert_kind)             \
    switch(const auto &cl_jit_instruction_switch_value = (instruction);        \
           convert_kind(cl_jit_instruction_switch_value.kind()))

#define CL_JIT_LEVEL_INSTRUCTION_CASE(Type, variable, convert_kind)            \
    convert_kind<Type>():                                                      \
    if(const Type variable = cl_jit_instruction_switch_value.as<Type>();       \
       false)                                                                  \
    {                                                                          \
    }                                                                          \
    else

#define CL_JIT_SEMANTIC_INSTRUCTION_SWITCH(instruction)                        \
    CL_JIT_LEVEL_INSTRUCTION_SWITCH(instruction, semantic_instruction_kind)
#define CL_JIT_SEMANTIC_INSTRUCTION_CASE(Type, variable)                       \
    CL_JIT_LEVEL_INSTRUCTION_CASE(                                             \
        Type, variable, semantic_instruction_kind)

#define CL_JIT_CORE_INSTRUCTION_SWITCH(instruction)                            \
    CL_JIT_LEVEL_INSTRUCTION_SWITCH(instruction, core_instruction_kind)
#define CL_JIT_CORE_INSTRUCTION_CASE(Type, variable)                           \
    CL_JIT_LEVEL_INSTRUCTION_CASE(Type, variable, core_instruction_kind)

#define CL_JIT_MACHINE_INSTRUCTION_SWITCH(instruction)                         \
    CL_JIT_LEVEL_INSTRUCTION_SWITCH(instruction, machine_instruction_kind)
#define CL_JIT_MACHINE_INSTRUCTION_CASE(Type, variable)                        \
    CL_JIT_LEVEL_INSTRUCTION_CASE(Type, variable, machine_instruction_kind)

    // clang-format on

    class TerminatorInstruction
    {
    public:
        using BlockSuccessorEdges = absl::InlinedVector<BlockEdge *, 2>;

        explicit TerminatorInstruction(Instruction instruction)
            : instruction_(instruction)
        {
            assert(instruction_.is_block_terminator());
        }

        InstructionKind kind() const { return instruction_.kind(); }
        BlockSuccessorEdges block_successor_edges() const;

    private:
        Instruction instruction_;
    };

    template <typename Visitor>
    void visit_operand_references(const Instruction &instruction,
                                  Visitor &&visitor)
    {
        const InstructionKindMetadata &metadata =
            instruction_kind_metadata(instruction.kind());
        assert(instruction.operands_are_indirect() ==
               metadata.operands_are_indirect);

        size_t slot_index = 0;
        assert(instruction.operand_count() >= metadata.fixed_operand_count);
        uint32_t variable_count =
            instruction.operand_count() - metadata.fixed_operand_count;
        uint32_t operand_index = 0;

        auto next_operand_word = [&] {
            assert(operand_index < instruction.operand_count());
            if(instruction.operands_are_indirect())
            {
                return instruction.operand_word(operand_index++);
            }
            ++operand_index;
            return instruction.slot(slot_index++);
        };

        auto visit_program_value =
            [&](uint32_t index, uint32_t word,
                ValueRepresentationRequirement representation) {
                visitor(index, OperandClass::ProgramValue, representation,
                        InstructionId(static_cast<uint32_t>(word)));
            };
        auto visit_snapshot = [&](uint32_t index, uint32_t word) {
            visitor(index, OperandClass::Snapshot,
                    ValueRepresentationRequirement::Any,
                    InstructionId(static_cast<uint32_t>(word)));
        };

        switch(instruction.kind())
        {
#define CL_JIT_IR_LEVELS(...)
#define CL_JIT_RESULT(...)
#define CL_JIT_EFFECT_BOUNDS(...)
#define CL_JIT_VISIT_FIXED_OPERAND(name, operand_class, representation)        \
    ([&] {                                                                     \
        uint32_t current_operand_index = operand_index;                        \
        uint32_t word = next_operand_word();                                   \
        if constexpr(OperandClass::operand_class ==                            \
                     OperandClass::ProgramValue)                               \
        {                                                                      \
            visit_program_value(current_operand_index, word,                   \
                                operand_representation_requirement<            \
                                    OperandClass::operand_class,               \
                                    ValueRepresentation::representation>());   \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            visit_snapshot(current_operand_index, word);                       \
        }                                                                      \
    }());
#define CL_JIT_VISIT_VARIADIC_OPERAND(name, operand_class, representation)     \
    ([&] {                                                                     \
        for(uint32_t index = 0; index < variable_count; ++index)               \
        {                                                                      \
            uint32_t current_operand_index = operand_index;                    \
            uint32_t word = next_operand_word();                               \
            if constexpr(OperandClass::operand_class ==                        \
                         OperandClass::ProgramValue)                           \
            {                                                                  \
                visit_program_value(                                           \
                    current_operand_index, word,                               \
                    operand_representation_requirement<                        \
                        OperandClass::operand_class,                           \
                        ValueRepresentation::representation>());               \
            }                                                                  \
            else                                                               \
            {                                                                  \
                visit_snapshot(current_operand_index, word);                   \
            }                                                                  \
        }                                                                      \
    }());
#define CL_JIT_VISIT_PROGRAM_VALUES(name, role)                                \
    ([&] {                                                                     \
        for(uint32_t index = 0; index < variable_count; ++index)               \
        {                                                                      \
            uint32_t current_operand_index = operand_index;                    \
            visit_program_value(current_operand_index, next_operand_word(),    \
                                ValueRepresentationRequirement::Any);          \
        }                                                                      \
    }());
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    case InstructionKind::name:                                                \
        operands(CL_JIT_VISIT_FIXED_OPERAND, CL_JIT_VISIT_VARIADIC_OPERAND,    \
                 CL_JIT_VISIT_PROGRAM_VALUES)                                  \
            assert(operand_index == instruction.operand_count());              \
        return;
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_VISIT_PROGRAM_VALUES
#undef CL_JIT_VISIT_VARIADIC_OPERAND
#undef CL_JIT_VISIT_FIXED_OPERAND
#undef CL_JIT_EFFECT_BOUNDS
#undef CL_JIT_RESULT
#undef CL_JIT_IR_LEVELS
        }
        assert(false);
    }

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_H
