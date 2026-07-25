#ifndef CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H
#define CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H

#include "jit/register_allocator.h"

#include <vector>

namespace cl::jit
{
    struct LiveRangeScan
    {
        std::vector<BlockProgramRange> block_ranges;
        std::vector<Occurrence> occurrences;
        std::vector<FixedLocationConstraint> fixed_constraints;
        std::vector<LiveRange> live_ranges;
        std::vector<ClobberReservation> clobbers;
    };

    Result<LiveRangeScan, RegisterAllocationError>
    scan_live_ranges(const ControlFlowGraph &graph,
                     const AllocationConstraints &constraints);

    PreparedAllocationProblem build_initial_bundles(LiveRangeScan scan);

}  // namespace cl::jit

#endif  // CL_JIT_REGISTER_ALLOCATOR_INTERNAL_H
