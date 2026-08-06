#ifndef CL_JIT_REGISTER_ALLOCATOR_H
#define CL_JIT_REGISTER_ALLOCATOR_H

#include "jit/allocation_constraints.h"
#include "jit/allocation_problem.h"
#include "jit/bundle_location_assignments.h"
#include "jit/location_assignments.h"
#include "util/result.h"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cl::jit
{
    class CompilationSession;

    enum class RegisterAllocationError : uint8_t
    {
        UnsupportedSnapshotConsumer,
        UnsupportedSameAsInput,
        UnsupportedTransferPoint,
        RequiresConstraintFixup,
        RequiresSplittingOrSpilling,
        InsufficientTransferScratchRegisters,
        MissingBytecodeStateOrder,
    };

    struct FixedOperandCopyFixup
    {
        OccurrenceId source;
        PhysicalRegister destination;
    };

    class RegisterAllocationResult
    {
    public:
        RegisterAllocationResult(
            std::vector<LiveBundle> bundles,
            BundleLocationAssignments locations,
            BundleTransferSchedule transfers,
            std::vector<FixedOperandCopyFixup> fixed_operand_copies,
            uint32_t spill_slot_count)
            : bundles_(std::move(bundles)), locations_(std::move(locations)),
              transfers_(std::move(transfers)),
              fixed_operand_copies_(std::move(fixed_operand_copies)),
              spill_slot_count_(spill_slot_count)
        {
        }

        std::span<const LiveBundle> bundles() const { return bundles_; }
        const BundleLocationAssignments &locations() const
        {
            return locations_;
        }
        const BundleTransferSchedule &transfers() const { return transfers_; }
        std::span<const FixedOperandCopyFixup> fixed_operand_copies() const
        {
            return fixed_operand_copies_;
        }
        uint32_t spill_slot_count() const { return spill_slot_count_; }

    private:
        std::vector<LiveBundle> bundles_;
        BundleLocationAssignments locations_;
        BundleTransferSchedule transfers_;
        std::vector<FixedOperandCopyFixup> fixed_operand_copies_;
        uint32_t spill_slot_count_;
    };

    class MaterializedAllocation
    {
    public:
        MaterializedAllocation(LocationAssignments locations,
                               uint32_t managed_frame_spill_extent)
            : locations_(std::move(locations)),
              managed_frame_spill_extent_(managed_frame_spill_extent)
        {
        }

        const LocationAssignments &locations() const { return locations_; }
        LocationAssignments take_locations() &&
        {
            return std::move(locations_);
        }
        uint32_t managed_frame_spill_extent() const
        {
            return managed_frame_spill_extent_;
        }

    private:
        LocationAssignments locations_;
        uint32_t managed_frame_spill_extent_;
    };

    Result<PreparedAllocationProblem, RegisterAllocationError>
    prepare_register_allocation(const ControlFlowGraph &graph,
                                const AllocationConstraints &constraints);

    Result<RegisterAllocationResult, RegisterAllocationError>
    assign_bundles(const PreparedAllocationProblem &problem,
                   const AllocationConstraints &constraints);

    Result<MaterializedAllocation, RegisterAllocationError>
    allocate_registers(CompilationSession &session, ControlFlowGraph &graph,
                       const AllocationConstraints &constraints);

    void verify_prepared_allocation(const PreparedAllocationProblem &problem);
    void
    verify_bundle_assignments(const PreparedAllocationProblem &problem,
                              const AllocationConstraints &constraints,
                              std::span<const LiveBundle> bundles,
                              const BundleLocationAssignments &assignments);
    void verify_register_allocation(const PreparedAllocationProblem &problem,
                                    const AllocationConstraints &constraints,
                                    const RegisterAllocationResult &allocation);

    std::string
    format_prepared_allocation(const PreparedAllocationProblem &problem);
    std::string
    format_bundle_assignments(const BundleLocationAssignments &assignments);
    std::string
    format_register_allocation(const PreparedAllocationProblem &problem,
                               const RegisterAllocationResult &allocation);

}  // namespace cl::jit

#endif  // CL_JIT_REGISTER_ALLOCATOR_H
