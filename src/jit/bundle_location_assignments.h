#ifndef CL_JIT_BUNDLE_LOCATION_ASSIGNMENTS_H
#define CL_JIT_BUNDLE_LOCATION_ASSIGNMENTS_H

#include "jit/allocation_problem.h"

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace cl::jit
{
    class BundleLocationAssignments
    {
    public:
        explicit BundleLocationAssignments(
            std::vector<PhysicalLocation> locations)
            : locations_(std::move(locations))
        {
        }

        size_t size() const { return locations_.size(); }

        PhysicalLocation location_for(BundleId bundle) const
        {
            assert(bundle.value() < locations_.size());
            return locations_[bundle.value()];
        }

    private:
        std::vector<PhysicalLocation> locations_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_BUNDLE_LOCATION_ASSIGNMENTS_H
