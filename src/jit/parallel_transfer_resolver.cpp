#include "jit/parallel_transfer_resolver.h"

#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

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

        using PendingSourceCounts =
            absl::flat_hash_map<PhysicalLocation, int32_t>;

        bool
        location_is_pending_source(PhysicalLocation location,
                                   const PendingSourceCounts &n_pending_sources)
        {
            return n_pending_sources.contains(location);
        }

        std::optional<PhysicalRegister>
        available_scratch(const ScratchRegisters &scratch_registers,
                          RegisterClass register_class,
                          const PendingSourceCounts &n_pending_sources)
        {
            for(PhysicalRegister scratch:
                scratch_registers[static_cast<size_t>(register_class)])
            {
                if(scratch.register_class() != register_class)
                {
                    fatal("parallel-transfer scratch has the wrong register "
                          "class");
                }
                if(!location_is_pending_source(PhysicalLocation::reg(scratch),
                                               n_pending_sources))
                {
                    return scratch;
                }
            }
            return std::nullopt;
        }

        size_t append_step(ResolvedTransferPlan &result,
                           ResolvedTransferSource source,
                           PhysicalLocation source_location,
                           PhysicalLocation destination,
                           int original_parallel_transfer_index)
        {
            size_t index = result.steps.size();
            result.steps.push_back({source, source_location, destination,
                                    original_parallel_transfer_index});
            return index;
        }

        Result<void, RegisterAllocationError>
        append_transfer(ResolvedTransferPlan &result,
                        const PendingTransfer &transfer,
                        std::optional<PhysicalRegister> scratch)
        {
            if(!transfer.source_location.is_stack() ||
               !transfer.destination.is_stack())
            {
                append_step(result, transfer.source, transfer.source_location,
                            transfer.destination, transfer.original_index);
                return Result<void, RegisterAllocationError>::ok();
            }

            if(!scratch.has_value())
            {
                return Result<void, RegisterAllocationError>::error(
                    RegisterAllocationError::
                        InsufficientTransferScratchRegisters);
            }
            size_t scratch_step =
                append_step(result, transfer.source, transfer.source_location,
                            PhysicalLocation::reg(*scratch), -1);
            append_step(result, ResolvedTransferSource::step(scratch_step),
                        PhysicalLocation::reg(*scratch), transfer.destination,
                        transfer.original_index);
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
            struct ReadyTransfer
            {
                size_t index;
                std::optional<PhysicalRegister> scratch;
            };
            std::optional<ReadyTransfer> ready;
            for(size_t index = 0; index < pending.size(); ++index)
            {
                const PendingTransfer &transfer = pending[index];
                if(location_is_pending_source(transfer.destination,
                                              n_pending_sources))
                {
                    continue;
                }
                std::optional<PhysicalRegister> scratch;
                if(transfer.source_location.is_stack() &&
                   transfer.destination.is_stack())
                {
                    scratch = available_scratch(scratch_registers,
                                                transfer.register_class,
                                                n_pending_sources);
                    if(!scratch.has_value())
                    {
                        continue;
                    }
                }
                ready = ReadyTransfer{index, scratch};
                break;
            }

            if(ready.has_value())
            {
                PendingTransfer transfer = pending[ready->index];
                auto appended =
                    append_transfer(result, transfer, ready->scratch);
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
                pending[ready->index] = std::move(pending.back());
                pending.pop_back();
                continue;
            }

            PendingTransfer &cycle = pending.front();
            std::optional<PhysicalRegister> scratch = available_scratch(
                scratch_registers, cycle.register_class, n_pending_sources);
            if(!scratch.has_value())
            {
                return Result<ResolvedTransferPlan, RegisterAllocationError>::
                    error(RegisterAllocationError::
                              InsufficientTransferScratchRegisters);
            }
            size_t scratch_step =
                append_step(result, cycle.source, cycle.source_location,
                            PhysicalLocation::reg(*scratch), -1);
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
