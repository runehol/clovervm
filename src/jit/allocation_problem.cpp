#include "jit/allocation_problem.h"

#include <utility>

namespace cl::jit
{
    PreparedAllocationProblem::PreparedAllocationProblem(
        std::vector<BlockProgramRange> block_ranges,
        std::vector<Occurrence> occurrences,
        std::vector<FixedLocationConstraint> fixed_constraints,
        std::vector<LiveRange> live_ranges, std::vector<LiveBundle> bundles,
        std::vector<ClobberReservation> clobbers)
        : block_ranges_(std::move(block_ranges)),
          occurrences_(std::move(occurrences)),
          fixed_constraints_(std::move(fixed_constraints)),
          live_ranges_(std::move(live_ranges)), bundles_(std::move(bundles)),
          clobbers_(std::move(clobbers))
    {
    }

}  // namespace cl::jit
