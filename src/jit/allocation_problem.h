#ifndef CL_JIT_ALLOCATION_PROBLEM_H
#define CL_JIT_ALLOCATION_PROBLEM_H

#include "jit/allocation_constraints.h"
#include "jit/control_flow_graph.h"
#include "runtime/fatal.h"
#include "util/dense_id.h"

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cl::jit
{
    struct FixedLocationConstraint;
    struct LiveBundle;
    struct LiveRange;
    struct Occurrence;

    using OccurrenceId = DenseId<Occurrence>;
    using LiveRangeId = DenseId<LiveRange>;
    using BundleId = DenseId<LiveBundle>;
    using FixedConstraintId = DenseId<FixedLocationConstraint>;

    class LivenessPosition
    {
    public:
        explicit constexpr LivenessPosition(size_t value) : value_(value) {}

        constexpr size_t value() const { return value_; }
        LivenessPosition next() const { return LivenessPosition(value_ + 1); }

        friend constexpr auto operator<=>(LivenessPosition,
                                          LivenessPosition) = default;

    private:
        size_t value_;
    };

    struct LivenessRange
    {
        LivenessPosition start;
        LivenessPosition end;

        size_t length() const
        {
            if(end < start)
            {
                fatal("inverted JIT allocator liveness range");
            }
            return end.value() - start.value();
        }
        bool contains(LivenessPosition point) const
        {
            return start <= point && point < end;
        }
        bool contains(LivenessRange range) const
        {
            return start <= range.start && range.end <= end;
        }
        bool empty() const { return start == end; }

        friend bool operator==(LivenessRange, LivenessRange) = default;
    };

    enum class OccurrenceKind : uint8_t
    {
        Use,
        Def,
        Temporary,
    };

    LivenessRange minimum_liveness_coverage(LivenessPosition instruction_early,
                                            OccurrenceKind kind,
                                            AccessTiming timing);

    class OccurrenceAnchor
    {
    public:
        enum class Kind : uint8_t
        {
            InstructionOperand,
            InstructionResult,
            BlockEdgeArgument,
            InstructionTemporary,
        };

        static OccurrenceAnchor instruction_operand(Instruction instruction,
                                                    size_t operand_index)
        {
            return {Kind::InstructionOperand, instruction.id(), operand_index};
        }
        static OccurrenceAnchor instruction_result(Instruction instruction)
        {
            return {Kind::InstructionResult, instruction.id(), 0};
        }
        static OccurrenceAnchor block_edge_argument(const BlockEdge *edge,
                                                    size_t argument_index)
        {
            assert(edge != nullptr);
            return {Kind::BlockEdgeArgument, edge, argument_index};
        }
        static OccurrenceAnchor instruction_temporary(Instruction instruction,
                                                      size_t temporary_index)
        {
            return {Kind::InstructionTemporary, instruction.id(),
                    temporary_index};
        }

        Kind kind() const { return kind_; }
        size_t index() const { return index_; }

        InstructionId instruction_id() const
        {
            assert(kind_ == Kind::InstructionOperand ||
                   kind_ == Kind::InstructionResult ||
                   kind_ == Kind::InstructionTemporary);
            return instruction_;
        }
        const BlockEdge *block_edge() const
        {
            assert(kind_ == Kind::BlockEdgeArgument);
            return static_cast<const BlockEdge *>(owner_);
        }

    private:
        OccurrenceAnchor(Kind kind, InstructionId instruction, size_t index)
            : kind_(kind), instruction_(instruction), owner_(nullptr),
              index_(index)
        {
        }
        OccurrenceAnchor(Kind kind, const BlockEdge *edge, size_t index)
            : kind_(kind), instruction_(0), owner_(edge), index_(index)
        {
        }

        Kind kind_;
        InstructionId instruction_;
        const void *owner_;
        size_t index_;
    };

    struct Occurrence
    {
        LivenessPosition position;
        // The irreducible liveness interval required by this occurrence.
        LivenessRange minimum_coverage;
        LiveRangeId live_range;
        OccurrenceKind kind;
        OccurrenceAnchor anchor;
        uint64_t spill_weight;
    };

    struct FixedLocationConstraint
    {
        LivenessPosition position;
        PhysicalLocation location;
        LiveRangeId live_range;
        OccurrenceId occurrence;
    };

    class LiveRangeOrigin
    {
    public:
        enum class Kind : uint8_t
        {
            ProgramValue,
            Temporary,
        };

        static LiveRangeOrigin program_value(Instruction definition)
        {
            assert(definition.result_class() == ResultClass::ProgramValue);
            return {Kind::ProgramValue, definition.id(), 0};
        }
        static LiveRangeOrigin temporary(Instruction instruction,
                                         size_t temporary_index)
        {
            return {Kind::Temporary, instruction.id(), temporary_index};
        }

        Kind kind() const { return kind_; }
        InstructionId instruction_id() const { return instruction_; }
        size_t temporary_index() const
        {
            assert(kind_ == Kind::Temporary);
            return index_;
        }
        ProgramValueRef program_value() const
        {
            assert(kind_ == Kind::ProgramValue);
            return ProgramValueRef(instruction_);
        }

    private:
        LiveRangeOrigin(Kind kind, InstructionId instruction, size_t index)
            : kind_(kind), instruction_(instruction), index_(index)
        {
        }

        Kind kind_;
        InstructionId instruction_;
        size_t index_;
    };

    struct LiveRange
    {
        LivenessRange range;
        LiveRangeOrigin origin;
        const Block *block;
        RegisterClass register_class;
        std::vector<OccurrenceId> occurrences;
        std::vector<FixedConstraintId> fixed_constraints;
    };

    struct BundleFragment
    {
        LivenessRange range;
        LiveRangeId source;
    };

    struct LiveBundle
    {
        RegisterClass register_class;
        std::vector<BundleFragment> fragments;
        std::vector<FixedConstraintId> fixed_constraints;
        size_t allocation_priority;
        uint64_t spill_weight;
    };

    class TransferPoint
    {
    public:
        enum class Kind : uint8_t
        {
            BeforeInstruction,
            BlockEntry,
            BlockExit,
            BlockEdge,
        };

        static TransferPoint before_instruction(Instruction instruction)
        {
            return TransferPoint(instruction.id());
        }
        static TransferPoint block_entry(const Block *block)
        {
            assert(block != nullptr);
            return {Kind::BlockEntry, block};
        }
        static TransferPoint block_exit(const Block *block)
        {
            assert(block != nullptr);
            return {Kind::BlockExit, block};
        }
        static TransferPoint block_edge(const BlockEdge *edge)
        {
            assert(edge != nullptr);
            return {Kind::BlockEdge, edge};
        }

        Kind kind() const { return kind_; }
        InstructionId instruction_id() const
        {
            assert(kind_ == Kind::BeforeInstruction);
            return instruction_;
        }
        const Block *block() const
        {
            assert(kind_ == Kind::BlockEntry || kind_ == Kind::BlockExit);
            return static_cast<const Block *>(owner_);
        }
        const BlockEdge *edge() const
        {
            assert(kind_ == Kind::BlockEdge);
            return static_cast<const BlockEdge *>(owner_);
        }

        friend bool operator==(TransferPoint, TransferPoint) = default;

    private:
        explicit TransferPoint(InstructionId instruction)
            : kind_(Kind::BeforeInstruction), instruction_(instruction),
              owner_(nullptr)
        {
        }
        TransferPoint(Kind kind, const void *owner)
            : kind_(kind), instruction_(0), owner_(owner)
        {
        }

        Kind kind_;
        InstructionId instruction_;
        const void *owner_;
    };

    enum class TransferPhase : uint8_t
    {
        Regular,
    };

    struct BundleTransfer
    {
        BundleId source;
        BundleId destination;
    };

    struct BundleTransferSet
    {
        TransferPoint point;
        TransferPhase phase;
        std::vector<BundleTransfer> transfers;
    };

    class BundleTransferSchedule
    {
    public:
        void add(TransferPoint point, TransferPhase phase,
                 BundleTransfer transfer);

        const std::vector<BundleTransferSet> &sets() const { return sets_; }

    private:
        std::vector<BundleTransferSet> sets_;
    };

    bool bundles_overlap(const LiveBundle &lhs, const LiveBundle &rhs);

    struct ClobberReservation
    {
        LivenessRange range;
        PhysicalRegister reg;
        InstructionId instruction;
    };

    struct BlockLivenessRange
    {
        const Block *block;
        LivenessRange range;
    };

    struct EdgeAffinity
    {
        // Connects one predecessor edge use to its successor parameter def.
        const BlockEdge *edge;
        uint32_t argument_index;
        OccurrenceId source;
        OccurrenceId destination;
    };

    class PreparedAllocationProblem
    {
    public:
        PreparedAllocationProblem(
            std::vector<BlockLivenessRange> block_ranges,
            std::vector<Occurrence> occurrences,
            std::vector<FixedLocationConstraint> fixed_constraints,
            std::vector<LiveRange> live_ranges, std::vector<LiveBundle> bundles,
            std::vector<ClobberReservation> clobbers,
            std::vector<EdgeAffinity> edge_affinities);

        const std::vector<BlockLivenessRange> &block_ranges() const
        {
            return block_ranges_;
        }
        const std::vector<Occurrence> &occurrences() const
        {
            return occurrences_;
        }
        const std::vector<FixedLocationConstraint> &fixed_constraints() const
        {
            return fixed_constraints_;
        }
        const std::vector<LiveRange> &live_ranges() const
        {
            return live_ranges_;
        }
        const std::vector<LiveBundle> &bundles() const { return bundles_; }
        const std::vector<ClobberReservation> &clobbers() const
        {
            return clobbers_;
        }
        const std::vector<EdgeAffinity> &edge_affinities() const
        {
            return edge_affinities_;
        }

    private:
        std::vector<BlockLivenessRange> block_ranges_;
        std::vector<Occurrence> occurrences_;
        std::vector<FixedLocationConstraint> fixed_constraints_;
        std::vector<LiveRange> live_ranges_;
        std::vector<LiveBundle> bundles_;
        std::vector<ClobberReservation> clobbers_;
        std::vector<EdgeAffinity> edge_affinities_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_ALLOCATION_PROBLEM_H
