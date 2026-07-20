#ifndef CL_JIT_INSTRUCTION_H
#define CL_JIT_INSTRUCTION_H

#include "jit/serial.h"

#include <absl/container/inlined_vector.h>

#include <cstdint>
#include <initializer_list>

namespace cl::jit
{
    class BlockEdge;

    enum class TerminatorKind : uint8_t
    {
        ConditionalBranch,
        UnconditionalBranch,
        Return,
    };

    class Instruction
    {
    public:
        using Serial = TypedSerial<Instruction>;

        virtual ~Instruction() = default;

        Serial serial() const { return serial_; }
        bool is_block_terminator() const { return is_block_terminator_; }

    protected:
        explicit Instruction(Serial serial) : serial_(serial) {}

    private:
        friend class TerminatorInstruction;

        struct TerminatorTag
        {
        };

        Instruction(Serial serial, TerminatorTag)
            : serial_(serial), is_block_terminator_(true)
        {
        }

        Serial serial_;
        bool is_block_terminator_ = false;
    };

    class TerminatorInstruction : public Instruction
    {
    public:
        using BlockSuccessorEdges = absl::InlinedVector<BlockEdge *, 2>;

        TerminatorKind terminator_kind() const { return terminator_kind_; }

        const BlockSuccessorEdges &block_successor_edges() const
        {
            return block_successor_edges_;
        }

    protected:
        TerminatorInstruction(
            Serial serial, TerminatorKind terminator_kind,
            std::initializer_list<BlockEdge *> block_successor_edges)
            : Instruction(serial, TerminatorTag{}),
              terminator_kind_(terminator_kind),
              block_successor_edges_(block_successor_edges)
        {
        }

    private:
        TerminatorKind terminator_kind_;
        BlockSuccessorEdges block_successor_edges_;
    };

    class ConditionalBranchInstruction final : public TerminatorInstruction
    {
    public:
        ConditionalBranchInstruction(Serial serial, BlockEdge *true_edge,
                                     BlockEdge *false_edge)
            : TerminatorInstruction(serial, TerminatorKind::ConditionalBranch,
                                    {true_edge, false_edge})
        {
        }

        BlockEdge *true_edge() const { return block_successor_edges()[0]; }
        BlockEdge *false_edge() const { return block_successor_edges()[1]; }
    };

    class UnconditionalBranchInstruction final : public TerminatorInstruction
    {
    public:
        UnconditionalBranchInstruction(Serial serial, BlockEdge *edge)
            : TerminatorInstruction(serial, TerminatorKind::UnconditionalBranch,
                                    {edge})
        {
        }

        BlockEdge *edge() const { return block_successor_edges()[0]; }
    };

    class ReturnInstruction final : public TerminatorInstruction
    {
    public:
        explicit ReturnInstruction(Serial serial)
            : TerminatorInstruction(serial, TerminatorKind::Return, {})
        {
        }
    };

}  // namespace cl::jit

#endif  // CL_JIT_INSTRUCTION_H
