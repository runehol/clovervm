#include "jit/parallel_transfer_resolver.h"

#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct PendingTransfer
        {
            int original_index;
            ResolvedTransferSource source;
            PhysicalLocation source_location;
            PhysicalLocation destination;
            RegisterClass register_class;
        };

        std::optional<PhysicalRegister>
        scratch_for(const ScratchRegisters &scratch_registers,
                    RegisterClass register_class)
        {
            std::optional<PhysicalRegister> scratch =
                scratch_registers[static_cast<size_t>(register_class)];
            if(scratch.has_value() &&
               scratch->register_class() != register_class)
            {
                fatal("parallel-transfer scratch has the wrong register class");
            }
            return scratch;
        }

        using PendingSourceCounts =
            absl::flat_hash_map<PhysicalLocation, int32_t>;

        bool
        location_is_pending_source(PhysicalLocation location,
                                   const PendingSourceCounts &n_pending_sources)
        {
            return n_pending_sources.contains(location);
        }

        bool scratch_is_occupied(PhysicalRegister scratch,
                                 const PendingSourceCounts &n_pending_sources)
        {
            return location_is_pending_source(PhysicalLocation::reg(scratch),
                                              n_pending_sources);
        }

        bool cycle_is_all_stack(size_t start,
                                const std::vector<PendingTransfer> &pending)
        {
            size_t current = start;
            do
            {
                const PendingTransfer &transfer = pending[current];
                if(transfer.source_location.is_register() ||
                   transfer.destination.is_register())
                {
                    return false;
                }

                auto next = std::ranges::find_if(
                    pending, [&](const PendingTransfer &candidate) {
                        return transfer.destination.aliases(
                            candidate.source_location);
                    });
                if(next == pending.end())
                {
                    fatal("malformed parallel-transfer dependency cycle");
                }
                current = static_cast<size_t>(next - pending.begin());
            }
            while(current != start);
            return true;
        }

        size_t append_step(ResolvedTransferPlan &result,
                           ResolvedTransferSource source,
                           PhysicalLocation destination,
                           int original_parallel_transfer_index)
        {
            size_t index = result.steps.size();
            result.steps.push_back(
                {source, destination, original_parallel_transfer_index});
            return index;
        }

        Result<void, RegisterAllocationError>
        append_transfer(ResolvedTransferPlan &result,
                        const PendingTransfer &transfer,
                        const ScratchRegisters &scratch_registers,
                        const PendingSourceCounts &n_pending_sources)
        {
            if(!transfer.source_location.is_stack() ||
               !transfer.destination.is_stack())
            {
                append_step(result, transfer.source, transfer.destination,
                            transfer.original_index);
                return Result<void, RegisterAllocationError>::ok();
            }

            std::optional<PhysicalRegister> scratch =
                scratch_for(scratch_registers, transfer.register_class);
            if(!scratch.has_value() ||
               scratch_is_occupied(*scratch, n_pending_sources))
            {
                return Result<void, RegisterAllocationError>::error(
                    RegisterAllocationError::RequiresTransferScratch);
            }
            size_t scratch_step = append_step(
                result, transfer.source, PhysicalLocation::reg(*scratch), -1);
            append_step(result, ResolvedTransferSource::step(scratch_step),
                        transfer.destination, transfer.original_index);
            return Result<void, RegisterAllocationError>::ok();
        }
    }  // namespace

    Result<ResolvedTransferPlan, RegisterAllocationError>
    resolve_parallel_transfers(std::span<const ParallelTransfer> transfers,
                               const ScratchRegisters &scratch_registers)
    {
        ResolvedTransferPlan result;
        std::vector<PendingTransfer> pending;
        pending.reserve(transfers.size());
        PendingSourceCounts n_pending_sources;
        int original_index = 0;
        for(const ParallelTransfer &transfer: transfers)
        {
            if(transfer.register_class >= RegisterClass::Count ||
               (transfer.source.is_register() &&
                transfer.source.reg().register_class() !=
                    transfer.register_class) ||
               (transfer.destination.is_register() &&
                transfer.destination.reg().register_class() !=
                    transfer.register_class))
            {
                fatal("parallel transfer has an incompatible register class");
            }
            if(transfer.source.aliases(transfer.destination))
            {
                result.aliasing_transfers.push_back(original_index);
                ++original_index;
                continue;
            }
            pending.push_back(
                {original_index,
                 ResolvedTransferSource::original_transfer(original_index),
                 transfer.source, transfer.destination,
                 transfer.register_class});
            ++n_pending_sources[transfer.source];
            ++original_index;
        }

        while(!pending.empty())
        {
            std::optional<size_t> ready;
            for(size_t index = 0; index < pending.size(); ++index)
            {
                const PendingTransfer &transfer = pending[index];
                if(location_is_pending_source(transfer.destination,
                                              n_pending_sources))
                {
                    continue;
                }
                if(transfer.source_location.is_stack() &&
                   transfer.destination.is_stack())
                {
                    std::optional<PhysicalRegister> scratch =
                        scratch_for(scratch_registers, transfer.register_class);
                    if(scratch.has_value() &&
                       scratch_is_occupied(*scratch, n_pending_sources))
                    {
                        continue;
                    }
                }
                ready = index;
                break;
            }

            if(ready.has_value())
            {
                PendingTransfer transfer = pending[*ready];
                auto appended = append_transfer(
                    result, transfer, scratch_registers, n_pending_sources);
                if(!appended)
                {
                    return propagate_failure(std::move(appended));
                }
                auto source_count =
                    n_pending_sources.find(transfer.source_location);
                if(source_count == n_pending_sources.end() ||
                   source_count->second <= 0)
                {
                    fatal("invalid parallel-transfer pending-source count");
                }
                if(--source_count->second == 0)
                {
                    n_pending_sources.erase(source_count);
                }
                pending[*ready] = std::move(pending.back());
                pending.pop_back();
                continue;
            }

            if(cycle_is_all_stack(0, pending))
            {
                return Result<ResolvedTransferPlan, RegisterAllocationError>::
                    error(RegisterAllocationError::RequiresTransferSpillSlot);
            }

            PendingTransfer &cycle = pending.front();
            std::optional<PhysicalRegister> scratch =
                scratch_for(scratch_registers, cycle.register_class);
            if(!scratch.has_value() ||
               scratch_is_occupied(*scratch, n_pending_sources))
            {
                return Result<ResolvedTransferPlan, RegisterAllocationError>::
                    error(RegisterAllocationError::RequiresTransferScratch);
            }
            size_t scratch_step = append_step(
                result, cycle.source, PhysicalLocation::reg(*scratch), -1);
            PhysicalLocation old_source = cycle.source_location;
            int32_t moved_sources = 0;
            for(PendingTransfer &transfer: pending)
            {
                if(transfer.source_location.aliases(old_source))
                {
                    transfer.source =
                        ResolvedTransferSource::step(scratch_step);
                    transfer.source_location = PhysicalLocation::reg(*scratch);
                    ++moved_sources;
                }
            }
            auto old_source_count = n_pending_sources.find(old_source);
            if(old_source_count == n_pending_sources.end() ||
               old_source_count->second != moved_sources)
            {
                fatal("invalid parallel-transfer cycle source count");
            }
            n_pending_sources.erase(old_source_count);
            n_pending_sources.emplace(PhysicalLocation::reg(*scratch),
                                      moved_sources);
        }

        return Result<ResolvedTransferPlan, RegisterAllocationError>::ok(
            std::move(result));
    }

}  // namespace cl::jit
