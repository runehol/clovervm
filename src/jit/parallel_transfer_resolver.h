#ifndef CL_JIT_PARALLEL_TRANSFER_RESOLVER_H
#define CL_JIT_PARALLEL_TRANSFER_RESOLVER_H

#include "jit/physical_location.h"
#include "jit/register_allocator.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace cl::jit
{
    // One assignment in a simultaneous physical-location shuffle. The value
    // initially in source must end in destination; register_class selects the
    // compatible scratch-register bank used while ordering the shuffle.
    struct ParallelTransfer
    {
        PhysicalLocation source;
        PhysicalLocation destination;
        RegisterClass register_class;
    };

    using ScratchRegisters =
        std::array<std::vector<PhysicalRegister>,
                   static_cast<size_t>(RegisterClass::Count)>;

    class ResolvedTransferSource
    {
    public:
        enum class Kind : uint8_t
        {
            OriginalTransfer,
            Step,
        };

        static ResolvedTransferSource original_transfer(size_t index)
        {
            return {Kind::OriginalTransfer, index};
        }
        static ResolvedTransferSource step(size_t index)
        {
            return {Kind::Step, index};
        }

        Kind kind() const { return kind_; }
        size_t index() const { return index_; }

    private:
        ResolvedTransferSource(Kind kind, size_t index)
            : kind_(kind), index_(index)
        {
        }

        Kind kind_;
        size_t index_;
    };

    struct ResolvedTransferStep
    {
        ResolvedTransferSource source;
        PhysicalLocation source_location;
        PhysicalLocation destination;
        int original_parallel_transfer_index;
    };

    struct ResolvedTransferPlan
    {
        std::vector<size_t> aliasing_transfers;
        std::vector<ResolvedTransferStep> steps;
    };

    Result<ResolvedTransferPlan, RegisterAllocationError>
    resolve_parallel_transfers(std::span<const ParallelTransfer> transfers,
                               const ScratchRegisters &scratch_registers);

}  // namespace cl::jit

#endif  // CL_JIT_PARALLEL_TRANSFER_RESOLVER_H
