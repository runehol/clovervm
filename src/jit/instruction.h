#ifndef CL_JIT_INSTRUCTION_H
#define CL_JIT_INSTRUCTION_H

#include "jit/serial.h"
#include "object_model/value.h"

#include <absl/container/inlined_vector.h>
#include <absl/types/span.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace cl::jit
{
    class BlockEdge;
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
        EffectProfile must_effects;
        EffectProfile may_effects;
        uint8_t fixed_operand_count;
        uint8_t attribute_count;
        uint8_t inline_slot_count;
        bool has_variadic_operands;
    };

    const InstructionKindMetadata &
    instruction_kind_metadata(InstructionKind kind);

    class Instruction final
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
            assert(!is_detached());
            InstructionKind result = static_cast<InstructionKind>(kind_);
            assert(is_valid_instruction_kind(result));
            return result;
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

    private:
        friend class InstructionPool;

        Instruction(uint32_t serial, InstructionKind kind,
                    uint16_t operand_count, bool indirect_operands,
                    absl::Span<const Slot> inline_slots)
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

    class InlineValueConstant
    {
    public:
        explicit InlineValueConstant(Value value) : value_(value)
        {
            assert(!value.is_ptr());
        }

        Value value() const { return value_; }

    private:
        Value value_;
    };

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

    class ProgramValueOperand
    {
    public:
        ProgramValueOperand(ProgramValueRef reference)
            : word_(reinterpret_cast<uintptr_t>(reference.instruction()))
        {
            assert(is_reference());
        }

        ProgramValueOperand(InlineValueConstant constant)
            : word_(static_cast<uintptr_t>(constant.value().as.integer))
        {
            assert(!is_reference());
        }

        bool is_reference() const
        {
            return (word_ & value_interned_ptr_tag) != 0;
        }

        ProgramValueRef reference() const
        {
            assert(is_reference());
            return ProgramValueRef(reinterpret_cast<Instruction *>(word_));
        }

        InlineValueConstant inline_constant() const
        {
            assert(!is_reference());
            Value value;
            value.as.integer = static_cast<long long>(word_);
            return InlineValueConstant(value);
        }

        uintptr_t raw_word() const { return word_; }

    private:
        explicit ProgramValueOperand(uintptr_t word) : word_(word) {}

        friend ProgramValueOperand
        program_value_operand_from_raw(uintptr_t word);

        uintptr_t word_;
    };

    inline ProgramValueOperand program_value_operand_from_raw(uintptr_t word)
    {
        return ProgramValueOperand(word);
    }

    inline uintptr_t instruction_reference_word(Instruction *instruction)
    {
        assert(instruction != nullptr);
        uintptr_t word = reinterpret_cast<uintptr_t>(instruction);
        assert((word & value_interned_ptr_tag) != 0);
        return word;
    }

    class ConditionalBranchInstruction
    {
    public:
        explicit ConditionalBranchInstruction(const Instruction *instruction)
            : instruction_(instruction)
        {
            assert(instruction_->kind() == InstructionKind::ConditionalBranch);
        }

        ProgramValueOperand condition() const
        {
            return program_value_operand_from_raw(instruction_->slot(0));
        }

        BlockEdge *true_edge() const
        {
            return reinterpret_cast<BlockEdge *>(instruction_->slot(1));
        }

        BlockEdge *false_edge() const
        {
            return reinterpret_cast<BlockEdge *>(instruction_->slot(2));
        }

    private:
        const Instruction *instruction_;
    };

    class UnconditionalBranchInstruction
    {
    public:
        explicit UnconditionalBranchInstruction(const Instruction *instruction)
            : instruction_(instruction)
        {
            assert(instruction_->kind() ==
                   InstructionKind::UnconditionalBranch);
        }

        BlockEdge *edge() const
        {
            return reinterpret_cast<BlockEdge *>(instruction_->slot(0));
        }

    private:
        const Instruction *instruction_;
    };

    class ReturnInstruction
    {
    public:
        explicit ReturnInstruction(const Instruction *instruction)
            : instruction_(instruction)
        {
            assert(instruction_->kind() == InstructionKind::Return);
        }

        ProgramValueOperand value() const
        {
            return program_value_operand_from_raw(instruction_->slot(0));
        }

    private:
        const Instruction *instruction_;
    };

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
        uint16_t variable_count =
            instruction.operand_count() - metadata.fixed_operand_count;
        uint16_t operand_index = 0;

        auto next_operand_word = [&] {
            assert(operand_index < instruction.operand_count());
            if(instruction.operands_are_indirect())
            {
                return operand_words[operand_index++];
            }
            ++operand_index;
            return instruction.slot(slot_index++);
        };

        auto visit_program_value = [&](uintptr_t word) {
            ProgramValueOperand operand = program_value_operand_from_raw(word);
            if(operand.is_reference())
            {
                visitor(OperandClass::ProgramValue,
                        operand.reference().instruction());
            }
        };
        auto visit_snapshot = [&](uintptr_t word) {
            Instruction *producer = reinterpret_cast<Instruction *>(word);
            assert(producer != nullptr);
            assert(producer->result_class() == ResultClass::Snapshot);
            visitor(OperandClass::Snapshot, producer);
        };

        switch(instruction.kind())
        {
#define CL_JIT_IR_LEVELS(...)
#define CL_JIT_RESULT(...)
#define CL_JIT_EFFECT_BOUNDS(...)
#define CL_JIT_ATTRIBUTES(...)
#define CL_JIT_ATTRIBUTE(...)
#define CL_JIT_OPERANDS(...) __VA_ARGS__;
#define CL_JIT_OPERAND(name, operand_class, representation)                    \
    ([&] {                                                                     \
        uintptr_t word = next_operand_word();                                  \
        if constexpr(OperandClass::operand_class ==                            \
                     OperandClass::ProgramValue)                               \
        {                                                                      \
            visit_program_value(word);                                         \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            visit_snapshot(word);                                              \
        }                                                                      \
    }())
#define CL_JIT_VARIADIC_OPERAND(name, operand_class, representation)           \
    ([&] {                                                                     \
        for(uint16_t index = 0; index < variable_count; ++index)               \
        {                                                                      \
            uintptr_t word = next_operand_word();                              \
            if constexpr(OperandClass::operand_class ==                        \
                         OperandClass::ProgramValue)                           \
            {                                                                  \
                visit_program_value(word);                                     \
            }                                                                  \
            else                                                               \
            {                                                                  \
                visit_snapshot(word);                                          \
            }                                                                  \
        }                                                                      \
    }())
#define CL_JIT_SNAPSHOT_VALUES(name)                                           \
    ([&] {                                                                     \
        assert(variable_count == 0 &&                                          \
               "annotated Snapshot traversal is not implemented yet");         \
    }())
#define CL_JIT_INSTRUCTION(name, ir_levels, result, effects, operands,         \
                           attributes)                                         \
    case InstructionKind::name:                                                \
        operands assert(operand_index == instruction.operand_count());         \
        return;
#include "jit/instruction.def"
#undef CL_JIT_INSTRUCTION
#undef CL_JIT_SNAPSHOT_VALUES
#undef CL_JIT_VARIADIC_OPERAND
#undef CL_JIT_OPERAND
#undef CL_JIT_OPERANDS
#undef CL_JIT_ATTRIBUTE
#undef CL_JIT_ATTRIBUTES
#undef CL_JIT_EFFECT_BOUNDS
#undef CL_JIT_RESULT
#undef CL_JIT_IR_LEVELS
        }
        assert(false);
    }

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_H
