#ifndef CL_JIT_ALLOCATION_PROBLEM_H
#define CL_JIT_ALLOCATION_PROBLEM_H

#include "jit/allocation_constraints.h"
#include "jit/control_flow_graph.h"
#include "runtime/fatal.h"

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl::jit
{
    namespace register_allocator_detail
    {
        template <typename Tag> class DenseId
        {
        public:
            explicit constexpr DenseId(size_t value) : value_(value) {}

            constexpr size_t value() const { return value_; }

            friend constexpr auto operator<=>(DenseId, DenseId) = default;

        private:
            size_t value_;
        };
    }  // namespace register_allocator_detail

    struct OccurrenceIdTag;
    struct LiveRangeIdTag;
    struct BundleIdTag;
    struct FixedConstraintIdTag;

    using OccurrenceId = register_allocator_detail::DenseId<OccurrenceIdTag>;
    using LiveRangeId = register_allocator_detail::DenseId<LiveRangeIdTag>;
    using BundleId = register_allocator_detail::DenseId<BundleIdTag>;
    using FixedConstraintId =
        register_allocator_detail::DenseId<FixedConstraintIdTag>;

    class ProgramPoint
    {
    public:
        explicit constexpr ProgramPoint(size_t value) : value_(value) {}

        constexpr size_t value() const { return value_; }
        ProgramPoint next() const { return ProgramPoint(value_ + 1); }

        friend constexpr auto operator<=>(ProgramPoint, ProgramPoint) = default;

    private:
        size_t value_;
    };

    struct ProgramRange
    {
        ProgramPoint start;
        ProgramPoint end;

        size_t length() const
        {
            if(end < start)
            {
                fatal("inverted JIT allocator program range");
            }
            return end.value() - start.value();
        }
        bool contains(ProgramPoint point) const
        {
            return start <= point && point < end;
        }
        bool contains(ProgramRange range) const
        {
            return start <= range.start && range.end <= end;
        }
        bool empty() const { return start == end; }
    };

    enum class OccurrenceKind : uint8_t
    {
        Use,
        Def,
        Temporary,
    };

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

        static OccurrenceAnchor
        instruction_operand(const Instruction *instruction,
                            size_t operand_index)
        {
            assert(instruction != nullptr);
            return {Kind::InstructionOperand, instruction, operand_index};
        }
        static OccurrenceAnchor
        instruction_result(const Instruction *instruction)
        {
            assert(instruction != nullptr);
            return {Kind::InstructionResult, instruction, 0};
        }
        static OccurrenceAnchor block_edge_argument(const BlockEdge *edge,
                                                    size_t argument_index)
        {
            assert(edge != nullptr);
            return {Kind::BlockEdgeArgument, edge, argument_index};
        }
        static OccurrenceAnchor
        instruction_temporary(const Instruction *instruction,
                              size_t temporary_index)
        {
            assert(instruction != nullptr);
            return {Kind::InstructionTemporary, instruction, temporary_index};
        }

        Kind kind() const { return kind_; }
        const void *owner() const { return owner_; }
        size_t index() const { return index_; }

        const Instruction *instruction() const
        {
            assert(kind_ == Kind::InstructionOperand ||
                   kind_ == Kind::InstructionResult ||
                   kind_ == Kind::InstructionTemporary);
            return static_cast<const Instruction *>(owner_);
        }
        const BlockEdge *block_edge() const
        {
            assert(kind_ == Kind::BlockEdgeArgument);
            return static_cast<const BlockEdge *>(owner_);
        }

    private:
        OccurrenceAnchor(Kind kind, const void *owner, size_t index)
            : kind_(kind), owner_(owner), index_(index)
        {
        }

        Kind kind_;
        const void *owner_;
        size_t index_;
    };

    struct Occurrence
    {
        ProgramPoint point;
        LiveRangeId live_range;
        OccurrenceKind kind;
        OccurrenceAnchor anchor;
        uint64_t spill_weight;
    };

    struct FixedRegisterConstraint
    {
        ProgramPoint point;
        PhysicalRegister reg;
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

        static LiveRangeOrigin program_value(Instruction *definition)
        {
            assert(definition != nullptr);
            assert(definition->result_class() == ResultClass::ProgramValue);
            return {Kind::ProgramValue, definition, 0};
        }
        static LiveRangeOrigin temporary(Instruction *instruction,
                                         size_t temporary_index)
        {
            assert(instruction != nullptr);
            return {Kind::Temporary, instruction, temporary_index};
        }

        Kind kind() const { return kind_; }
        const Instruction *instruction() const { return instruction_; }
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
        LiveRangeOrigin(Kind kind, Instruction *instruction, size_t index)
            : kind_(kind), instruction_(instruction), index_(index)
        {
        }

        Kind kind_;
        Instruction *instruction_;
        size_t index_;
    };

    struct LiveRange
    {
        ProgramRange range;
        LiveRangeOrigin origin;
        const Block *block;
        RegisterClass register_class;
        std::vector<OccurrenceId> occurrences;
        std::vector<FixedConstraintId> fixed_constraints;
    };

    struct BundleFragment
    {
        ProgramRange range;
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

    bool bundles_overlap(const LiveBundle &lhs, const LiveBundle &rhs);

    struct ClobberReservation
    {
        ProgramRange range;
        PhysicalRegister reg;
        const Instruction *instruction;
    };

    struct BlockProgramRange
    {
        const Block *block;
        ProgramRange range;
    };

    class PreparedAllocationProblem
    {
    public:
        PreparedAllocationProblem(
            std::vector<BlockProgramRange> block_ranges,
            std::vector<Occurrence> occurrences,
            std::vector<FixedRegisterConstraint> fixed_constraints,
            std::vector<LiveRange> live_ranges, std::vector<LiveBundle> bundles,
            std::vector<ClobberReservation> clobbers);

        const std::vector<BlockProgramRange> &block_ranges() const
        {
            return block_ranges_;
        }
        const std::vector<Occurrence> &occurrences() const
        {
            return occurrences_;
        }
        const std::vector<FixedRegisterConstraint> &fixed_constraints() const
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

    private:
        std::vector<BlockProgramRange> block_ranges_;
        std::vector<Occurrence> occurrences_;
        std::vector<FixedRegisterConstraint> fixed_constraints_;
        std::vector<LiveRange> live_ranges_;
        std::vector<LiveBundle> bundles_;
        std::vector<ClobberReservation> clobbers_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_ALLOCATION_PROBLEM_H
