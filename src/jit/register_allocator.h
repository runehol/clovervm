#ifndef CL_JIT_REGISTER_ALLOCATOR_H
#define CL_JIT_REGISTER_ALLOCATOR_H

#include "jit/allocation_constraints.h"
#include "jit/allocation_problem.h"
#include "util/result.h"

#include <cstdint>
#include <string>

namespace cl::jit
{
    enum class RegisterAllocationError : uint8_t
    {
        UnsupportedSnapshotConsumer,
        UnsupportedSameAsInput,
    };

    Result<PreparedAllocationProblem, RegisterAllocationError>
    prepare_register_allocation(const ControlFlowGraph &graph,
                                const AllocationConstraints &constraints);

    void verify_prepared_allocation(const PreparedAllocationProblem &problem);

    std::string
    format_prepared_allocation(const PreparedAllocationProblem &problem);

}  // namespace cl::jit

#endif  // CL_JIT_REGISTER_ALLOCATOR_H
