#include "jit/parallel_transfer_resolver.h"

#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <deque>
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
            bool pending = true;
            bool queued = false;
        };

        using PendingSourceCounts =
            absl::flat_hash_map<PhysicalLocation, int32_t>;
        using TransferIndicesByLocation =
            absl::flat_hash_map<PhysicalLocation, std::vector<size_t>>;
        using TransferIndexByDestination =
            absl::flat_hash_map<PhysicalLocation, size_t>;

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

        class ParallelTransferResolver
        {
        public:
            ParallelTransferResolver(
                std::span<const ParallelTransfer> transfers,
                const ScratchRegisters &scratch_registers)
                : scratch_registers_(&scratch_registers)
            {
                pending_.reserve(transfers.size());
                int original_index = 0;
                for(const ParallelTransfer &transfer: transfers)
                {
                    if(transfer.source.aliases(transfer.destination))
                    {
                        result_.aliasing_transfers.push_back(original_index);
                        ++original_index;
                        continue;
                    }
                    if(transfers_by_destination_.contains(transfer.destination))
                    {
                        fatal(
                            "parallel transfers have a duplicate destination");
                    }

                    size_t pending_index = pending_.size();
                    pending_.push_back(
                        {original_index,
                         ResolvedTransferSource::original_transfer(
                             original_index),
                         transfer.source, transfer.destination,
                         transfer.register_class});
                    ++n_pending_sources_[transfer.source];
                    transfers_by_source_[transfer.source].push_back(
                        pending_index);
                    transfers_by_destination_.emplace(transfer.destination,
                                                      pending_index);
                    cycle_candidates_[class_index(transfer.register_class)]
                        .push_back(pending_index);
                    ++original_index;
                }
                n_pending_ = pending_.size();
                for(size_t index = 0; index < pending_.size(); ++index)
                {
                    enqueue_if_ready(index);
                }
            }

            Result<ResolvedTransferPlan, RegisterAllocationError> resolve()
            {
                while(n_pending_ != 0)
                {
                    if(!ready_.empty())
                    {
                        size_t index = ready_.front();
                        ready_.pop_front();
                        PendingTransfer &transfer = pending_[index];
                        transfer.queued = false;
                        std::optional<PhysicalRegister> scratch;
                        if(transfer.source_location.is_stack() &&
                           transfer.destination.is_stack())
                        {
                            scratch =
                                available_scratch(transfer.register_class);
                            if(!scratch.has_value())
                            {
                                return Result<ResolvedTransferPlan,
                                              RegisterAllocationError>::
                                    error(
                                        RegisterAllocationError::
                                            InsufficientTransferScratchRegisters);
                            }
                            size_t scratch_step = append_step(
                                result_, transfer.source,
                                transfer.source_location,
                                PhysicalLocation::reg(*scratch), -1);
                            append_step(
                                result_,
                                ResolvedTransferSource::step(scratch_step),
                                PhysicalLocation::reg(*scratch),
                                transfer.destination, transfer.original_index);
                        }
                        else
                        {
                            append_step(result_, transfer.source,
                                        transfer.source_location,
                                        transfer.destination,
                                        transfer.original_index);
                        }
                        complete(index);
                        continue;
                    }

                    std::optional<CycleBreak> cycle = next_cycle();
                    if(!cycle.has_value())
                    {
                        return Result<ResolvedTransferPlan,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      InsufficientTransferScratchRegisters);
                    }
                    break_cycle(*cycle);
                }
                return Result<ResolvedTransferPlan,
                              RegisterAllocationError>::ok(std::move(result_));
            }

        private:
            struct CycleBreak
            {
                size_t transfer;
                PhysicalRegister scratch;
            };

            static size_t class_index(RegisterClass register_class)
            {
                return static_cast<size_t>(register_class);
            }

            std::optional<PhysicalRegister>
            available_scratch(RegisterClass register_class) const
            {
                for(PhysicalRegister scratch:
                    (*scratch_registers_)[class_index(register_class)])
                {
                    if(!n_pending_sources_.contains(
                           PhysicalLocation::reg(scratch)))
                    {
                        return scratch;
                    }
                }
                return std::nullopt;
            }

            void enqueue_if_ready(size_t index)
            {
                PendingTransfer &transfer = pending_[index];
                if(!transfer.pending || transfer.queued ||
                   n_pending_sources_.contains(transfer.destination))
                {
                    return;
                }
                transfer.queued = true;
                ready_.push_back(index);
            }

            void source_became_free(PhysicalLocation source)
            {
                auto destinations = transfers_by_destination_.find(source);
                if(destinations != transfers_by_destination_.end())
                {
                    enqueue_if_ready(destinations->second);
                }
            }

            void complete(size_t index)
            {
                PendingTransfer &transfer = pending_[index];
                if(!transfer.pending)
                {
                    fatal("parallel transfer completed twice");
                }
                transfer.pending = false;
                --n_pending_;

                PhysicalLocation source = transfer.source_location;
                auto count = n_pending_sources_.find(source);
                if(count == n_pending_sources_.end() || count->second <= 0)
                {
                    fatal("invalid parallel-transfer pending-source count");
                }
                if(--count->second != 0)
                {
                    return;
                }
                n_pending_sources_.erase(count);
                transfers_by_source_.erase(source);
                source_became_free(source);
            }

            std::optional<CycleBreak> next_cycle()
            {
                for(size_t class_number = 0;
                    class_number < static_cast<size_t>(RegisterClass::Count);
                    ++class_number)
                {
                    RegisterClass register_class =
                        static_cast<RegisterClass>(class_number);
                    std::optional<PhysicalRegister> scratch =
                        available_scratch(register_class);
                    if(!scratch.has_value())
                    {
                        continue;
                    }
                    std::deque<size_t> &candidates =
                        cycle_candidates_[class_number];
                    while(!candidates.empty())
                    {
                        size_t index = candidates.front();
                        candidates.pop_front();
                        const PendingTransfer &transfer = pending_[index];
                        if(!transfer.pending ||
                           !n_pending_sources_.contains(transfer.destination))
                        {
                            continue;
                        }
                        return CycleBreak{index, *scratch};
                    }
                }
                return std::nullopt;
            }

            void break_cycle(CycleBreak cycle)
            {
                PendingTransfer &selected = pending_[cycle.transfer];
                size_t scratch_step = append_step(
                    result_, selected.source, selected.source_location,
                    PhysicalLocation::reg(cycle.scratch), -1);
                PhysicalLocation old_source = selected.source_location;

                auto source_users = transfers_by_source_.find(old_source);
                if(source_users == transfers_by_source_.end())
                {
                    fatal("parallel-transfer cycle source has no users");
                }
                std::vector<size_t> users = std::move(source_users->second);
                transfers_by_source_.erase(source_users);

                int32_t moved_sources = 0;
                for(size_t index: users)
                {
                    PendingTransfer &transfer = pending_[index];
                    if(!transfer.pending ||
                       !transfer.source_location.aliases(old_source))
                    {
                        continue;
                    }
                    transfer.source =
                        ResolvedTransferSource::step(scratch_step);
                    transfer.source_location =
                        PhysicalLocation::reg(cycle.scratch);
                    transfers_by_source_[transfer.source_location].push_back(
                        index);
                    ++moved_sources;
                }

                auto old_source_count = n_pending_sources_.find(old_source);
                if(old_source_count == n_pending_sources_.end() ||
                   old_source_count->second != moved_sources)
                {
                    fatal("invalid parallel-transfer cycle source count");
                }
                n_pending_sources_.erase(old_source_count);
                n_pending_sources_.emplace(PhysicalLocation::reg(cycle.scratch),
                                           moved_sources);
                source_became_free(old_source);
            }

            const ScratchRegisters *scratch_registers_;
            ResolvedTransferPlan result_;
            std::vector<PendingTransfer> pending_;
            size_t n_pending_ = 0;
            PendingSourceCounts n_pending_sources_;
            TransferIndicesByLocation transfers_by_source_;
            TransferIndexByDestination transfers_by_destination_;
            std::deque<size_t> ready_;
            std::array<std::deque<size_t>,
                       static_cast<size_t>(RegisterClass::Count)>
                cycle_candidates_;
        };
    }  // namespace

    Result<ResolvedTransferPlan, RegisterAllocationError>
    resolve_parallel_transfers(std::span<const ParallelTransfer> transfers,
                               const ScratchRegisters &scratch_registers)
    {
        return ParallelTransferResolver(transfers, scratch_registers).resolve();
    }

}  // namespace cl::jit
