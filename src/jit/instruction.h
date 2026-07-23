#ifndef CL_JIT_INSTRUCTION_H
#define CL_JIT_INSTRUCTION_H

#include "jit/serial.h"
#include "object_model/shape_key.h"
#include "object_model/value.h"

#include <absl/container/inlined_vector.h>
#include <span>

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
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
    class GraphRewriter;
    class InstructionPool;

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
        Count,
    };

    enum class EffectProfile : uint8_t
    {
        None,
        Deoptimize,
        Allocate,
        AllocateOrRaise,
        CallPython,
        ConservativeCall,
        ExitJIT,
        TerminateBlock,
    };

    enum class IRLevelMask : uint8_t
    {
        None = 0,
        Semantic = 1 << 0,
        Core = 1 << 1,
        Machine = 1 << 2,
    };

    constexpr IRLevelMask operator|(IRLevelMask lhs, IRLevelMask rhs)
    {
        return static_cast<IRLevelMask>(static_cast<uint8_t>(lhs) |
                                        static_cast<uint8_t>(rhs));
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

    constexpr InstructionOrdinal instruction_ordinal(InstructionKind kind)
    {
        return static_cast<InstructionOrdinal>(static_cast<uint16_t>(kind) &
                                               InstructionOrdinalMask);
    }

    constexpr ResultClass instruction_result_class(InstructionKind kind)
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
                   representation == ValueRepresentation::F64;
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
        IRLevelMask allowed_ir_levels;
        EffectProfile must_effects;
        EffectProfile may_effects;
        uint8_t fixed_operand_count;
        uint8_t attribute_count;
        uint8_t inline_slot_count;
        bool has_variadic_operands;
    };

    const InstructionKindMetadata &
    instruction_kind_metadata(InstructionKind kind);

    class Instruction
    {
    public:
        using Serial = TypedSerial<Instruction>;
        using Slot = uintptr_t;

        static constexpr size_t InlineSlotCount = 5;
        static constexpr uint16_t IndirectOperandsBit = uint16_t{1} << 15;
        static constexpr uint16_t OperandCountMask = IndirectOperandsBit - 1;
        static constexpr uint16_t DetachedStorageTag = UINT16_MAX;

        Instruction(const Instruction &) = delete;
        Instruction &operator=(const Instruction &) = delete;
        Instruction(Instruction &&) = delete;
        Instruction &operator=(Instruction &&) = delete;

        Serial serial() const { return Serial(serial_); }
        bool is_detached() const { return kind_ == DetachedStorageTag; }

        InstructionKind kind() const
        {
            if(is_detached())
            {
                fatal_detached_access();
            }
            InstructionKind result = static_cast<InstructionKind>(kind_);
            assert(is_valid_instruction_kind(result));
            return result;
        }

        template <typename ConcreteInstruction> ConcreteInstruction *as()
        {
            assert(kind() == ConcreteInstruction::Kind);
            return static_cast<ConcreteInstruction *>(this);
        }

        template <typename ConcreteInstruction>
        const ConcreteInstruction *as() const
        {
            assert(kind() == ConcreteInstruction::Kind);
            return static_cast<const ConcreteInstruction *>(this);
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
            return instruction_kind_metadata(kind()).must_effects ==
                   EffectProfile::TerminateBlock;
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

    protected:
        friend class GraphRewriter;
        friend class InstructionPool;

        Instruction(uint32_t serial, InstructionKind kind,
                    uint16_t operand_count, bool indirect_operands,
                    std::span<const Slot> inline_slots)
            : serial_(serial), kind_(static_cast<uint16_t>(kind)),
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
        Instruction(uint32_t serial, InstructionKind kind,
                    uint16_t operand_count, bool indirect_operands,
                    const std::array<Slot, N> &inline_slots)
            : Instruction(serial, kind, operand_count, indirect_operands,
                          std::span<const Slot>(inline_slots))
        {
        }

        template <bool Indirect> Slot operand_word_at(size_t index) const
        {
            assert(index < operand_count());
            if constexpr(Indirect)
            {
                const Slot *operands =
                    reinterpret_cast<const Slot *>(slots_[0]);
                assert(operands != nullptr);
                return operands[index];
            }
            return slots_[index];
        }

        const Slot *indirect_operand_words() const
        {
            assert(operands_are_indirect());
            const Slot *operands = reinterpret_cast<const Slot *>(slots_[0]);
            assert(operands != nullptr || operand_count() == 0);
            return operands;
        }

        template <size_t Index> Slot inline_word_at() const
        {
            static_assert(Index < InlineSlotCount);
            return slots_[Index];
        }

    private:
        [[noreturn]] static void fatal_detached_access();

        void detach_and_poison()
        {
            assert(!is_detached());
            kind_ = DetachedStorageTag;
            operand_storage_ = DetachedStorageTag;
            for(Slot &slot: slots_)
            {
                slot = UINTPTR_MAX;
            }
        }

        uint32_t serial_;
        uint16_t kind_;
        uint16_t operand_storage_;
        Slot slots_[InlineSlotCount];
    };

    static_assert(sizeof(Instruction) == 48);
    static_assert(alignof(Instruction) == alignof(uintptr_t));
    static_assert(std::is_trivially_destructible_v<Instruction>);
    static_assert(static_cast<uint16_t>(InstructionOrdinal::Count) <=
                  InstructionOrdinalMask + 1);
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    static_assert(                                                             \
        instruction_kind_has_valid_result_encoding(InstructionKind::name));
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
    static_assert(!is_valid_instruction_kind(
        static_cast<InstructionKind>(Instruction::DetachedStorageTag)));

    class ProgramValueRef
    {
    public:
        explicit ProgramValueRef(Instruction *instruction)
            : instruction_(instruction)
        {
            assert(instruction != nullptr);
            assert(instruction->result_class() == ResultClass::ProgramValue);
        }

        Instruction *instruction() const { return instruction_; }

    private:
        Instruction *instruction_;
    };

    class SnapshotRef
    {
    public:
        explicit SnapshotRef(Instruction *instruction)
            : instruction_(instruction)
        {
            assert(instruction != nullptr);
            assert(instruction->result_class() == ResultClass::Snapshot);
        }

        Instruction *instruction() const { return instruction_; }

    private:
        Instruction *instruction_;
    };

    inline uintptr_t instruction_reference_word(Instruction *instruction)
    {
        assert(instruction != nullptr);
        return reinterpret_cast<uintptr_t>(instruction);
    }

    template <ValueRepresentation Representation> class RepresentedValueRef
    {
    public:
        explicit RepresentedValueRef(Instruction *instruction)
            : reference_(instruction)
        {
            assert(instruction->value_representation() == Representation);
        }

        Instruction *instruction() const { return reference_.instruction(); }
        operator ProgramValueRef() const { return reference_; }

    private:
        ProgramValueRef reference_;
    };

    using TaggedValueRef =
        RepresentedValueRef<ValueRepresentation::TaggedValue>;
    using F64Ref = RepresentedValueRef<ValueRepresentation::F64>;

    template <OperandClass Class, ValueRepresentation Representation>
    auto decode_instruction_operand(uintptr_t word)
    {
        if constexpr(Class == OperandClass::Snapshot)
        {
            static_assert(Representation == ValueRepresentation::None);
            return SnapshotRef(reinterpret_cast<Instruction *>(word));
        }
        else if constexpr(Representation == ValueRepresentation::TaggedValue)
        {
            return TaggedValueRef(reinterpret_cast<Instruction *>(word));
        }
        else
        {
            static_assert(Representation == ValueRepresentation::F64);
            return F64Ref(reinterpret_cast<Instruction *>(word));
        }
    }

    template <ValueRepresentation Representation> class ProgramValueRefRange
    {
    public:
        ProgramValueRefRange(const uintptr_t *words, size_t size)
            : words_(words), size_(size)
        {
            assert(words != nullptr || size == 0);
        }

        size_t size() const { return size_; }
        bool empty() const { return size_ == 0; }

        RepresentedValueRef<Representation> operator[](size_t index) const
        {
            assert(index < size_);
            return decode_instruction_operand<OperandClass::ProgramValue,
                                              Representation>(words_[index]);
        }

    private:
        const uintptr_t *words_;
        size_t size_;
    };

    class SnapshotValueRefRange
    {
    public:
        SnapshotValueRefRange(const uintptr_t *words, size_t size)
            : words_(words), size_(size)
        {
            assert(words != nullptr || size == 0);
        }

        size_t size() const { return size_; }
        bool empty() const { return size_ == 0; }

        ProgramValueRef operator[](size_t index) const
        {
            assert(index < size_);
            return ProgramValueRef(
                reinterpret_cast<Instruction *>(words_[index]));
        }

    private:
        const uintptr_t *words_;
        size_t size_;
    };

    using BytecodePC = uint32_t;

    inline Shape *decode_instruction_attribute_Shape(uintptr_t word)
    {
        return reinterpret_cast<Shape *>(word);
    }

    inline ValidityCell *
    decode_instruction_attribute_ValidityCell(uintptr_t word)
    {
        return reinterpret_cast<ValidityCell *>(word);
    }

    inline ShapeKey decode_instruction_attribute_ShapeKey(uintptr_t word)
    {
        static_assert(sizeof(ShapeKey) == sizeof(word));
        static_assert(std::is_trivially_copyable_v<ShapeKey>);
        return std::bit_cast<ShapeKey>(word);
    }

    inline Value decode_instruction_attribute_ValueConstant(uintptr_t word)
    {
        Value value;
        value.as.integer = static_cast<long long>(word);
        return value;
    }

    inline BytecodePC decode_instruction_attribute_BytecodePC(uintptr_t word)
    {
        return static_cast<BytecodePC>(word);
    }

    inline BlockEdge *decode_instruction_attribute_BlockEdge(uintptr_t word)
    {
        return reinterpret_cast<BlockEdge *>(word);
    }

    inline uintptr_t encode_instruction_operand(TaggedValueRef reference)
    {
        return instruction_reference_word(reference.instruction());
    }

    inline uintptr_t encode_instruction_operand(ProgramValueRef reference)
    {
        return instruction_reference_word(reference.instruction());
    }

    inline uintptr_t encode_instruction_operand(F64Ref reference)
    {
        return instruction_reference_word(reference.instruction());
    }

    inline uintptr_t encode_instruction_operand(SnapshotRef reference)
    {
        return instruction_reference_word(reference.instruction());
    }

    inline uintptr_t encode_instruction_attribute_Shape(Shape *shape)
    {
        assert(shape != nullptr);
        return reinterpret_cast<uintptr_t>(shape);
    }

    inline uintptr_t
    encode_instruction_attribute_ValidityCell(ValidityCell *validity)
    {
        assert(validity != nullptr);
        return reinterpret_cast<uintptr_t>(validity);
    }

    inline uintptr_t encode_instruction_attribute_ShapeKey(ShapeKey shape_key)
    {
        static_assert(sizeof(ShapeKey) == sizeof(uintptr_t));
        static_assert(std::is_trivially_copyable_v<ShapeKey>);
        return std::bit_cast<uintptr_t>(shape_key);
    }

    inline uintptr_t encode_instruction_attribute_ValueConstant(Value value)
    {
        return static_cast<uintptr_t>(value.as.integer);
    }

    inline uintptr_t encode_instruction_attribute_BytecodePC(BytecodePC pc)
    {
        return static_cast<uintptr_t>(pc);
    }

    inline uintptr_t encode_instruction_attribute_BlockEdge(BlockEdge *edge)
    {
        assert(edge != nullptr);
        return reinterpret_cast<uintptr_t>(edge);
    }

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
    //         EffectProfile::Deoptimize;
    //     static constexpr IRLevelMask AllowedIRLevels = IRLevelMask::Core;
    //     static constexpr bool IsVariadic = false;
    //
    //     TaggedValueRef object() const;
    //     SnapshotRef snapshot() const;
    //     Shape *expected_shape() const;
    //     ValidityCell *validity() const;
    //
    // private:
    //     friend class InstructionPool;
    //     ShapeGuardInstruction(uint32_t serial, TaggedValueRef object,
    //                           SnapshotRef snapshot, Shape *expected_shape,
    //                           ValidityCell *validity);
    // };
    //
    // Variadic classes additionally expose n_indirect_slots_for(...), and
    // their private constructor receives the arena-allocated indirect span
    // after serial. The macros below generate these declarations, their slot
    // encoders, and the accessor definitions from instruction.def.

    // clang-format off
#define CL_JIT_JOIN_INNER(first, second) first##second
#define CL_JIT_JOIN(first, second) CL_JIT_JOIN_INNER(first, second)
#define CL_JIT_OPERAND_TYPE_ProgramValue_TaggedValue TaggedValueRef
#define CL_JIT_OPERAND_TYPE_ProgramValue_F64 F64Ref
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
#define CL_JIT_ATTRIBUTE_TYPE_BlockEdge BlockEdge *
#define CL_JIT_ATTRIBUTE_TYPE(attribute_class)                                 \
    CL_JIT_JOIN(CL_JIT_ATTRIBUTE_TYPE_, attribute_class)
#define CL_JIT_DECLARE_OPERAND_INDEX(name, operand_class, representation) name,
#define CL_JIT_DECLARE_VARIADIC_INDEX(name, operand_class, representation) name,
#define CL_JIT_DECLARE_SNAPSHOT_VALUES_INDEX(name) name,
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
#define CL_JIT_COUNT_FIXED_OPERAND(...) +1
#define CL_JIT_COUNT_NO_OPERAND(...) +0
#define CL_JIT_HAS_NO_VARIADIC(...) || false
#define CL_JIT_HAS_VARIADIC(...) || true
#define CL_JIT_DECLARE_FIXED_PARAMETER(name, operand_class, representation)    \
    CL_JIT_OPERAND_TYPE(operand_class, representation) name,
#define CL_JIT_DECLARE_VARIADIC_PARAMETER(name, operand_class, representation) \
    std::span<const CL_JIT_OPERAND_TYPE(operand_class, representation)> name,
#define CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER(name)                         \
    std::span<const ProgramValueRef> name,
#define CL_JIT_DECLARE_ATTRIBUTE_PARAMETER(name, attribute_class)              \
    CL_JIT_ATTRIBUTE_TYPE(attribute_class) name,
#define CL_JIT_PASS_ARGUMENT(name, ...) name,
#define CL_JIT_IGNORE_ARGUMENT(name, ...) (void)name;
#define CL_JIT_COUNT_INDIRECT_FIXED(name, ...) (void)name;
#define CL_JIT_COUNT_INDIRECT_VARIADIC(name, ...) n_slots += name.size();
#define CL_JIT_COUNT_INDIRECT_SNAPSHOT_VALUES(name) n_slots += name.size();
#define CL_JIT_COUNT_LOGICAL_FIXED(name, ...) (void)name;
#define CL_JIT_COUNT_LOGICAL_VARIADIC(name, ...) n_operands += name.size();
#define CL_JIT_COUNT_LOGICAL_SNAPSHOT_VALUES(name) n_operands += name.size();
#define CL_JIT_ENCODE_FIXED_INLINE(name, ...) encode_instruction_operand(name),
#define CL_JIT_SKIP_INLINE(...)
#define CL_JIT_ENCODE_ATTRIBUTE_INLINE(name, attribute_class)                  \
    encode_instruction_attribute_##attribute_class(name),
#define CL_JIT_WRITE_INDIRECT_FIXED(name, ...)                                 \
    indirect_slots[index++] = encode_instruction_operand(name);
#define CL_JIT_WRITE_INDIRECT_VARIADIC(name, ...)                              \
    for(const auto &operand: name)                                             \
    {                                                                          \
        indirect_slots[index++] = encode_instruction_operand(operand);         \
    }
#define CL_JIT_WRITE_INDIRECT_SNAPSHOT_VALUES(name)                            \
    for(ProgramValueRef value: name)                                           \
    {                                                                          \
        indirect_slots[index++] = encode_instruction_operand(value);           \
    }
#define CL_JIT_DECLARE_ATTRIBUTE_INDEX(name, attribute_class) name,
#define CL_JIT_PRIVATE private:
#define CL_JIT_DECLARE_FIXED_ACCESSOR(name, operand_class, representation)     \
    auto name() const                                                          \
    {                                                                          \
        constexpr size_t index = static_cast<size_t>(OperandIndex::name);      \
        return decode_instruction_operand<                                     \
            OperandClass::operand_class, ValueRepresentation::representation>( \
            operand_word_at<IsVariadic>(index));                               \
    }
#define CL_JIT_DECLARE_VARIADIC_ACCESSOR(name, operand_class, representation)  \
    auto name() const                                                          \
    {                                                                          \
        static_assert(OperandClass::operand_class ==                           \
                      OperandClass::ProgramValue);                             \
        constexpr size_t index = static_cast<size_t>(OperandIndex::name);      \
        const Slot *words = indirect_operand_words();                          \
        const Slot *first = words == nullptr ? nullptr : words + index;        \
        return ProgramValueRefRange<ValueRepresentation::representation>(      \
            first, operand_count() - index);                                   \
    }
#define CL_JIT_DECLARE_SNAPSHOT_VALUES_ACCESSOR(name)                          \
    SnapshotValueRefRange name() const                                         \
    {                                                                          \
        constexpr size_t index = static_cast<size_t>(OperandIndex::name);      \
        const Slot *words = indirect_operand_words();                          \
        const Slot *first = words == nullptr ? nullptr : words + index;        \
        return SnapshotValueRefRange(first, operand_count() - index);          \
    }
#define CL_JIT_DECLARE_ATTRIBUTE_ACCESSOR(name, attribute_class)               \
    auto name() const                                                          \
    {                                                                          \
        constexpr size_t index =                                               \
            AttributeBase + static_cast<size_t>(AttributeIndex::name);         \
        return decode_instruction_attribute_##attribute_class(                 \
            inline_word_at<index>());                                          \
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
                     CL_JIT_DECLARE_SNAPSHOT_VALUES_INDEX) Count,              \
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
                                                                               \
        template <bool Variadic = IsVariadic>                                  \
        requires(Variadic)                                                     \
        static size_t n_indirect_slots_for(                                    \
            operands(CL_JIT_DECLARE_FIXED_PARAMETER,                           \
                     CL_JIT_DECLARE_VARIADIC_PARAMETER,                        \
                     CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER)                 \
                attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)                 \
                    InstructionConstructorEnd = {})                            \
        {                                                                      \
            size_t n_operands = FixedOperandCount;                             \
            operands(CL_JIT_COUNT_LOGICAL_FIXED,                              \
                     CL_JIT_COUNT_LOGICAL_VARIADIC,                           \
                     CL_JIT_COUNT_LOGICAL_SNAPSHOT_VALUES)                    \
            attributes(CL_JIT_IGNORE_ARGUMENT)                                \
            assert(n_operands <= OperandCountMask);                            \
            (void)n_operands;                                                  \
            size_t n_slots = FixedOperandCount;                                \
            operands(CL_JIT_COUNT_INDIRECT_FIXED,                              \
                     CL_JIT_COUNT_INDIRECT_VARIADIC,                           \
                     CL_JIT_COUNT_INDIRECT_SNAPSHOT_VALUES)                    \
            attributes(CL_JIT_IGNORE_ARGUMENT)                                \
            return n_slots;                                                    \
        }                                                                      \
                                                                               \
    operands(CL_JIT_DECLARE_FIXED_ACCESSOR, CL_JIT_DECLARE_VARIADIC_ACCESSOR,  \
             CL_JIT_DECLARE_SNAPSHOT_VALUES_ACCESSOR)                          \
        attributes(CL_JIT_DECLARE_ATTRIBUTE_ACCESSOR)                          \
                                                                               \
    CL_JIT_PRIVATE                                                             \
        static constexpr size_t AttributeBase =                                \
            IsVariadic ? 1 : FixedOperandCount;                               \
        static constexpr size_t InlineSlotCountForKind =                       \
            AttributeBase + AttributeCount;                                    \
        static_assert(InlineSlotCountForKind <= InlineSlotCount);              \
                                                                               \
        static uint16_t                                                        \
        operand_count_for(operands(CL_JIT_DECLARE_FIXED_PARAMETER,             \
                                   CL_JIT_DECLARE_VARIADIC_PARAMETER,          \
                                   CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER)   \
                              attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)   \
                                  InstructionConstructorEnd = {})              \
        {                                                                      \
            size_t n_operands = FixedOperandCount;                             \
            operands(CL_JIT_COUNT_LOGICAL_FIXED,                               \
                     CL_JIT_COUNT_LOGICAL_VARIADIC,                            \
                     CL_JIT_COUNT_LOGICAL_SNAPSHOT_VALUES)                     \
            attributes(CL_JIT_IGNORE_ARGUMENT)                                \
            assert(n_operands <= OperandCountMask);                            \
            return static_cast<uint16_t>(n_operands);                          \
        }                                                                      \
                                                                               \
        static std::array<Slot, FixedOperandCount + AttributeCount>            \
        fixed_inline_slots(operands(CL_JIT_DECLARE_FIXED_PARAMETER,            \
                                    CL_JIT_DECLARE_VARIADIC_PARAMETER,         \
                                    CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER)  \
                               attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)  \
                                   InstructionConstructorEnd = {})             \
        {                                                                      \
            return {operands(CL_JIT_ENCODE_FIXED_INLINE, CL_JIT_SKIP_INLINE,   \
                             CL_JIT_SKIP_INLINE)                               \
                        attributes(CL_JIT_ENCODE_ATTRIBUTE_INLINE)};           \
        }                                                                      \
                                                                               \
        static Slot *initialize_indirect_slots(                                \
            std::span<Slot> indirect_slots,                                   \
            operands(CL_JIT_DECLARE_FIXED_PARAMETER,                           \
                     CL_JIT_DECLARE_VARIADIC_PARAMETER,                        \
                     CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER)                 \
                attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)                 \
                    InstructionConstructorEnd = {})                            \
        {                                                                      \
            size_t index = 0;                                                  \
            operands(CL_JIT_WRITE_INDIRECT_FIXED,                              \
                     CL_JIT_WRITE_INDIRECT_VARIADIC,                           \
                     CL_JIT_WRITE_INDIRECT_SNAPSHOT_VALUES)                    \
            attributes(CL_JIT_IGNORE_ARGUMENT)                                \
            (void)index;                                                       \
            assert(index == indirect_slots.size());                            \
            return indirect_slots.data();                                      \
        }                                                                      \
                                                                               \
        static std::array<Slot, 1 + AttributeCount> indirect_inline_slots(     \
            std::span<Slot> indirect_slots,                                   \
            operands(CL_JIT_DECLARE_FIXED_PARAMETER,                           \
                     CL_JIT_DECLARE_VARIADIC_PARAMETER,                        \
                     CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER)                 \
                attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)                 \
                    InstructionConstructorEnd = {})                            \
        {                                                                      \
            Slot *stored_operands = initialize_indirect_slots(                 \
                indirect_slots,                                                \
                operands(CL_JIT_PASS_ARGUMENT, CL_JIT_PASS_ARGUMENT,           \
                         CL_JIT_PASS_ARGUMENT)                                 \
                    attributes(CL_JIT_PASS_ARGUMENT){});                       \
            return {reinterpret_cast<Slot>(stored_operands),                   \
                    attributes(CL_JIT_ENCODE_ATTRIBUTE_INLINE)};               \
        }                                                                      \
                                                                               \
        friend class InstructionPool;                                          \
        template <bool Variadic = IsVariadic>                                  \
        requires(!Variadic)                                                    \
        name##Instruction(uint32_t serial,                                     \
                          operands(CL_JIT_DECLARE_FIXED_PARAMETER,             \
                                   CL_JIT_DECLARE_VARIADIC_PARAMETER,          \
                                   CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER)   \
                              attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)   \
                                  InstructionConstructorEnd = {})              \
            : Instruction(                                                     \
                  serial, Kind, static_cast<uint16_t>(FixedOperandCount),      \
                  false,                                                       \
                  fixed_inline_slots(                                          \
                      operands(CL_JIT_PASS_ARGUMENT, CL_JIT_PASS_ARGUMENT,     \
                               CL_JIT_PASS_ARGUMENT)                           \
                          attributes(CL_JIT_PASS_ARGUMENT){}))                 \
        {                                                                      \
        }                                                                      \
                                                                               \
        template <bool Variadic = IsVariadic>                                  \
        requires(Variadic)                                                     \
        name##Instruction(uint32_t serial, std::span<Slot> indirect_slots,    \
                          operands(CL_JIT_DECLARE_FIXED_PARAMETER,             \
                                   CL_JIT_DECLARE_VARIADIC_PARAMETER,          \
                                   CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER)   \
                              attributes(CL_JIT_DECLARE_ATTRIBUTE_PARAMETER)   \
                                  InstructionConstructorEnd = {})              \
            : Instruction(                                                     \
                  serial, Kind,                                                \
                  operand_count_for(                                           \
                      operands(CL_JIT_PASS_ARGUMENT, CL_JIT_PASS_ARGUMENT,     \
                               CL_JIT_PASS_ARGUMENT)                           \
                          attributes(CL_JIT_PASS_ARGUMENT){}),                 \
                  true,                                                        \
                  indirect_inline_slots(                                       \
                      indirect_slots,                                          \
                      operands(CL_JIT_PASS_ARGUMENT, CL_JIT_PASS_ARGUMENT,     \
                               CL_JIT_PASS_ARGUMENT)                           \
                          attributes(CL_JIT_PASS_ARGUMENT){}))                 \
        {                                                                      \
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
#undef CL_JIT_EFFECT_BOUNDS
#undef CL_JIT_RESULT
#undef CL_JIT_IR_LEVELS
#undef CL_JIT_SELECT_IR_LEVELS
#undef CL_JIT_IR_LEVELS_THREE
#undef CL_JIT_IR_LEVELS_TWO
#undef CL_JIT_IR_LEVELS_ONE
#undef CL_JIT_DECLARE_ATTRIBUTE_ACCESSOR
#undef CL_JIT_DECLARE_SNAPSHOT_VALUES_ACCESSOR
#undef CL_JIT_DECLARE_VARIADIC_ACCESSOR
#undef CL_JIT_DECLARE_FIXED_ACCESSOR
#undef CL_JIT_PRIVATE
#undef CL_JIT_DECLARE_ATTRIBUTE_INDEX
#undef CL_JIT_HAS_VARIADIC
#undef CL_JIT_HAS_NO_VARIADIC
#undef CL_JIT_COUNT_NO_OPERAND
#undef CL_JIT_COUNT_FIXED_OPERAND
#undef CL_JIT_DECLARE_SNAPSHOT_VALUES_INDEX
#undef CL_JIT_DECLARE_VARIADIC_INDEX
#undef CL_JIT_DECLARE_OPERAND_INDEX
#undef CL_JIT_WRITE_INDIRECT_SNAPSHOT_VALUES
#undef CL_JIT_WRITE_INDIRECT_VARIADIC
#undef CL_JIT_WRITE_INDIRECT_FIXED
#undef CL_JIT_ENCODE_ATTRIBUTE_INLINE
#undef CL_JIT_SKIP_INLINE
#undef CL_JIT_ENCODE_FIXED_INLINE
#undef CL_JIT_COUNT_LOGICAL_SNAPSHOT_VALUES
#undef CL_JIT_COUNT_LOGICAL_VARIADIC
#undef CL_JIT_COUNT_LOGICAL_FIXED
#undef CL_JIT_COUNT_INDIRECT_SNAPSHOT_VALUES
#undef CL_JIT_COUNT_INDIRECT_VARIADIC
#undef CL_JIT_COUNT_INDIRECT_FIXED
#undef CL_JIT_IGNORE_ARGUMENT
#undef CL_JIT_PASS_ARGUMENT
#undef CL_JIT_DECLARE_ATTRIBUTE_PARAMETER
#undef CL_JIT_DECLARE_SNAPSHOT_VALUES_PARAMETER
#undef CL_JIT_DECLARE_VARIADIC_PARAMETER
#undef CL_JIT_DECLARE_FIXED_PARAMETER
#undef CL_JIT_ATTRIBUTE_TYPE
#undef CL_JIT_ATTRIBUTE_TYPE_BlockEdge
#undef CL_JIT_ATTRIBUTE_TYPE_BytecodePC
#undef CL_JIT_ATTRIBUTE_TYPE_ValueConstant
#undef CL_JIT_ATTRIBUTE_TYPE_ShapeKey
#undef CL_JIT_ATTRIBUTE_TYPE_ValidityCell
#undef CL_JIT_ATTRIBUTE_TYPE_Shape
#undef CL_JIT_OPERAND_TYPE
#undef CL_JIT_OPERAND_TYPE_INNER
#undef CL_JIT_OPERAND_TYPE_Snapshot_None
#undef CL_JIT_OPERAND_TYPE_ProgramValue_F64
#undef CL_JIT_OPERAND_TYPE_ProgramValue_TaggedValue
#undef CL_JIT_JOIN
#undef CL_JIT_JOIN_INNER
    // clang-format on

// Preserve a compiler-visible switch while binding each case to the checked,
// read-only concrete instruction type named by that case.
// clang-format off
#define CL_JIT_INSTRUCTION_SWITCH(instruction)                                 \
    switch(const auto &cl_jit_instruction_switch_value = (instruction);        \
           cl_jit_instruction_switch_value.kind())

#define CL_JIT_INSTRUCTION_CASE(Type, variable)                                \
    Type::Kind:                                                                \
    if(const Type &variable =                                                  \
           *cl_jit_instruction_switch_value.as<Type>();                        \
       false)                                                                  \
    {                                                                          \
    }                                                                          \
    else
    // clang-format on

    class TerminatorInstruction
    {
    public:
        using BlockSuccessorEdges = absl::InlinedVector<BlockEdge *, 2>;

        explicit TerminatorInstruction(const Instruction *instruction)
            : instruction_(instruction)
        {
            assert(instruction_->is_block_terminator());
        }

        InstructionKind kind() const { return instruction_->kind(); }
        BlockSuccessorEdges block_successor_edges() const;

    private:
        const Instruction *instruction_;
    };

    template <typename Visitor>
    void visit_operand_references(const Instruction &instruction,
                                  Visitor &&visitor)
    {
        const InstructionKindMetadata &metadata =
            instruction_kind_metadata(instruction.kind());
        assert(instruction.operands_are_indirect() ==
               metadata.has_variadic_operands);

        size_t slot_index = 0;
        const uintptr_t *operand_words = nullptr;
        if(instruction.operands_are_indirect())
        {
            operand_words = reinterpret_cast<const uintptr_t *>(
                instruction.slot(slot_index++));
            assert(operand_words != nullptr ||
                   instruction.operand_count() == 0);
        }
        assert(instruction.operand_count() >= metadata.fixed_operand_count);
        uint32_t variable_count =
            instruction.operand_count() - metadata.fixed_operand_count;
        uint32_t operand_index = 0;

        auto next_operand_word = [&] {
            assert(operand_index < instruction.operand_count());
            if(instruction.operands_are_indirect())
            {
                return operand_words[operand_index++];
            }
            ++operand_index;
            return instruction.slot(slot_index++);
        };

        auto visit_program_value = [&](uint32_t index, uintptr_t word,
                                       ValueRepresentation representation) {
            Instruction *def = reinterpret_cast<Instruction *>(word);
            assert(def != nullptr);
            assert(def->result_class() == ResultClass::ProgramValue);
            visitor(index, OperandClass::ProgramValue, representation, def);
        };
        auto visit_snapshot = [&](uint32_t index, uintptr_t word) {
            Instruction *def = reinterpret_cast<Instruction *>(word);
            assert(def != nullptr);
            assert(def->result_class() == ResultClass::Snapshot);
            visitor(index, OperandClass::Snapshot, ValueRepresentation::None,
                    def);
        };

        switch(instruction.kind())
        {
#define CL_JIT_IR_LEVELS(...)
#define CL_JIT_RESULT(...)
#define CL_JIT_EFFECT_BOUNDS(...)
#define CL_JIT_VISIT_FIXED_OPERAND(name, operand_class, representation)        \
    ([&] {                                                                     \
        uint32_t current_operand_index = operand_index;                        \
        uintptr_t word = next_operand_word();                                  \
        if constexpr(OperandClass::operand_class ==                            \
                     OperandClass::ProgramValue)                               \
        {                                                                      \
            visit_program_value(current_operand_index, word,                   \
                                ValueRepresentation::representation);          \
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
            uintptr_t word = next_operand_word();                              \
            if constexpr(OperandClass::operand_class ==                        \
                         OperandClass::ProgramValue)                           \
            {                                                                  \
                visit_program_value(current_operand_index, word,               \
                                    ValueRepresentation::representation);      \
            }                                                                  \
            else                                                               \
            {                                                                  \
                visit_snapshot(current_operand_index, word);                   \
            }                                                                  \
        }                                                                      \
    }());
#define CL_JIT_VISIT_SNAPSHOT_VALUES(name)                                     \
    ([&] {                                                                     \
        for(uint32_t index = 0; index < variable_count; ++index)               \
        {                                                                      \
            uint32_t current_operand_index = operand_index;                    \
            visit_program_value(current_operand_index, next_operand_word(),    \
                                ValueRepresentation::None);                    \
        }                                                                      \
    }());
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    case InstructionKind::name:                                                \
        operands(CL_JIT_VISIT_FIXED_OPERAND, CL_JIT_VISIT_VARIADIC_OPERAND,    \
                 CL_JIT_VISIT_SNAPSHOT_VALUES)                                 \
            assert(operand_index == instruction.operand_count());              \
        return;
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_VISIT_SNAPSHOT_VALUES
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
