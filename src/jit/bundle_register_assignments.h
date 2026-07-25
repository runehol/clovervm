#ifndef CL_JIT_BUNDLE_REGISTER_ASSIGNMENTS_H
#define CL_JIT_BUNDLE_REGISTER_ASSIGNMENTS_H

#include "jit/allocation_problem.h"

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace cl::jit
{
    class BundleRegisterAssignments
    {
    public:
        explicit BundleRegisterAssignments(
            std::vector<PhysicalRegister> register_by_bundle)
            : register_by_bundle_(std::move(register_by_bundle))
        {
        }

        size_t size() const { return register_by_bundle_.size(); }

        PhysicalRegister register_for(BundleId bundle) const
        {
            assert(bundle.value() < register_by_bundle_.size());
            return register_by_bundle_[bundle.value()];
        }

    private:
        std::vector<PhysicalRegister> register_by_bundle_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_BUNDLE_REGISTER_ASSIGNMENTS_H
