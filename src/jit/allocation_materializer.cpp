#include "jit/allocation_materializer.h"

#include "jit/graph_rewriter.h"
#include "jit/instruction_reconstruction.h"
#include "jit/parallel_assignment_resolver.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        using ScratchRegisters =
            std::array<std::vector<PhysicalRegister>,
                       static_cast<size_t>(RegisterClass::Count)>;
        using FutureSourceCounts =
            absl::flat_hash_map<PhysicalLocation, int32_t>;

        struct PlannedTransferSet
        {
            std::vector<BundleTransfer> transfers;
            OrderedParallelAssignment<PhysicalLocation> ordered;
            std::vector<InstructionId> sources;
            std::vector<uint32_t> fixed_operand_indices;
        };

        struct PlannedEdgeTransferSequence
        {
            BlockEdge *edge;
            std::vector<PlannedTransferSet> phases;
            std::vector<PhysicalLocation> parameter_locations;
            std::vector<BundleId> parameter_bundles;
            std::vector<BundleId> outgoing_bundles;
        };

        struct PlannedEdgeTransferEntry
        {
            std::vector<PlannedTransferSet> phases;
            std::vector<BundleId> parameter_bundles;
            std::vector<BundleId> outgoing_bundles;
        };

        struct PlannedFixedOperandCopySet
        {
            std::vector<uint32_t> operand_indices;
            OrderedParallelAssignment<PhysicalLocation> ordered;
        };

        struct PendingFixedOperandReplacements
        {
            std::vector<uint32_t> operand_indices;
            std::vector<InstructionId> replacements;
        };

        struct MaterializationPlan
        {
            absl::flat_hash_map<const Block *, PlannedTransferSet>
                block_entries;
            absl::flat_hash_map<InstructionId, PlannedTransferSet>
                before_instructions;
            absl::flat_hash_map<InstructionId, PlannedFixedOperandCopySet>
                fixed_operand_copies;
            std::vector<PlannedEdgeTransferSequence> edge_transfers;
            absl::flat_hash_map<const Block *, PlannedEdgeTransferEntry>
                edge_entries;
            LocationAssignmentsBuilder existing_locations;
            std::vector<std::optional<InstructionId>> initial_value_by_bundle;
        };

        struct ResolvedBundleLocations
        {
            BundleLocationAssignments locations;
            uint32_t managed_frame_spill_extent;
        };

        Result<ResolvedBundleLocations, RegisterAllocationError>
        resolve_bundle_locations(const ControlFlowGraph &graph,
                                 const RegisterAllocationResult &allocation)
        {
            uint32_t spill_slot_count = allocation.spill_slot_count();
            uint32_t ordinary_extent = 0;
            if(spill_slot_count != 0)
            {
                if(!graph.bytecode_state_order().has_value())
                {
                    return Result<ResolvedBundleLocations,
                                  RegisterAllocationError>::
                        error(
                            RegisterAllocationError::MissingBytecodeStateOrder);
                }
                const BytecodeStateOrder &order = *graph.bytecode_state_order();
                ordinary_extent = round_up_to_abi_alignment(
                    order.n_locals() + order.n_temporaries());
            }

            std::vector<BundleLocation> locations;
            locations.reserve(allocation.locations().size());
            for(size_t index = 0; index < allocation.locations().size();
                ++index)
            {
                BundleLocation location =
                    allocation.locations().location_for(BundleId(index));
                if(location.is_physical())
                {
                    locations.push_back(location);
                    continue;
                }

                uint32_t slot = location.spill_slot().value();
                assert(slot < spill_slot_count);
                int32_t frame_offset = -int32_t(ordinary_extent + slot + 1);
                locations.push_back(BundleLocation::physical(
                    PhysicalLocation::stack(StackLocation(
                        StackLocationKind::SpillSlot, frame_offset))));
            }
            return Result<ResolvedBundleLocations, RegisterAllocationError>::ok(
                {BundleLocationAssignments(std::move(locations)),
                 spill_slot_count});
        }

        struct MaterializationPointIndex
        {
            absl::flat_hash_map<const Block *, LivenessPosition> block_entries;
            absl::flat_hash_map<InstructionId, LivenessPosition>
                before_instructions;
        };

        struct TransferSourceKey
        {
            BundleId bundle;
            LivenessPosition boundary;

            friend bool operator==(TransferSourceKey,
                                   TransferSourceKey) = default;

            template <typename H>
            friend H AbslHashValue(H hash, TransferSourceKey key)
            {
                return H::combine(std::move(hash), key.bundle,
                                  key.boundary.value());
            }
        };

        using TransferSourceIndex =
            absl::flat_hash_map<TransferSourceKey, InstructionId>;

        MaterializationPointIndex build_materialization_point_index(
            const PreparedAllocationProblem &problem)
        {
            MaterializationPointIndex result;
            result.block_entries.reserve(problem.block_ranges().size());
            size_t instruction_count = 0;
            for(const BlockLivenessRange &block_range: problem.block_ranges())
            {
                instruction_count += block_range.block->instructions().size();
            }
            result.before_instructions.reserve(instruction_count);
            for(const BlockLivenessRange &block_range: problem.block_ranges())
            {
                LivenessPosition block_start = block_range.range.start;
                result.block_entries.emplace(block_range.block, block_start);
                for(size_t index = 0;
                    index < block_range.block->instructions().size(); ++index)
                {
                    Instruction instruction =
                        block_range.block->instruction_at(index);
                    result.before_instructions.emplace(
                        instruction.id(),
                        LivenessPosition(block_start.value() + 2 + index * 2));
                }
            }
            return result;
        }

        std::optional<LivenessPosition>
        transfer_point_position(TransferPoint point,
                                const MaterializationPointIndex &index)
        {
            switch(point.kind())
            {
                case TransferPoint::Kind::BlockEntry:
                    return index.block_entries.at(point.block());
                case TransferPoint::Kind::BeforeInstruction:
                    return index.before_instructions.at(point.instruction_id());
                case TransferPoint::Kind::BlockExit:
                case TransferPoint::Kind::BlockEdge:
                    return std::nullopt;
            }
            fatal("invalid JIT transfer point");
        }

        TransferSourceIndex
        build_transfer_source_index(const PreparedAllocationProblem &problem,
                                    const RegisterAllocationResult &allocation)
        {
            TransferSourceIndex result;
            size_t source_count = 0;
            for(const LiveBundle &bundle: allocation.bundles())
            {
                for(const BundleFragment &fragment: bundle.fragments)
                {
                    const LiveRange &range =
                        problem.live_ranges()[fragment.source.value()];
                    if(range.origin.kind() ==
                       LiveRangeOrigin::Kind::ProgramValue)
                    {
                        ++source_count;
                    }
                }
            }
            result.reserve(source_count);
            for(size_t bundle_index = 0;
                bundle_index < allocation.bundles().size(); ++bundle_index)
            {
                BundleId bundle(static_cast<uint32_t>(bundle_index));
                for(const BundleFragment &fragment:
                    allocation.bundles()[bundle_index].fragments)
                {
                    const LiveRange &range =
                        problem.live_ranges()[fragment.source.value()];
                    if(range.origin.kind() !=
                       LiveRangeOrigin::Kind::ProgramValue)
                    {
                        continue;
                    }
                    TransferSourceKey key{bundle, fragment.range.end};
                    InstructionId source =
                        range.origin.program_value().instruction_id();
                    auto [position, inserted] = result.emplace(key, source);
                    if(!inserted && position->second != source)
                    {
                        fatal("JIT bundle transfer source index has ambiguous "
                              "source value");
                    }
                }
            }
            return result;
        }

        bool needs_explicit_transfer_sources(TransferPoint point)
        {
            return point.kind() == TransferPoint::Kind::BlockEntry ||
                   point.kind() == TransferPoint::Kind::BeforeInstruction;
        }

        std::vector<InstructionId>
        transfer_sources_at_point(const BundleTransferSet &set,
                                  const MaterializationPointIndex &point_index,
                                  const TransferSourceIndex &source_index)
        {
            std::optional<LivenessPosition> boundary =
                transfer_point_position(set.point, point_index);
            assert(boundary.has_value());

            std::vector<InstructionId> result;
            result.reserve(set.transfers.size());
            for(const BundleTransfer &transfer: set.transfers)
            {
                auto source = source_index.find({transfer.source, *boundary});
                if(source == source_index.end())
                {
                    fatal("JIT bundle transfer has no program value at its "
                          "boundary");
                }
                result.push_back(source->second);
            }
            return result;
        }

        size_t append_move(OrderedParallelAssignment<PhysicalLocation> &result,
                           OrderedMoveSource source,
                           PhysicalLocation source_location,
                           PhysicalLocation destination,
                           RegisterClass register_class,
                           int original_assignment_index)
        {
            size_t index = result.moves.size();
            result.moves.push_back({source, source_location, destination,
                                    register_class, original_assignment_index});
            return index;
        }

        std::optional<PhysicalRegister>
        available_scratch(const ScratchRegisters &scratch_registers,
                          RegisterClass register_class,
                          const FutureSourceCounts &future_sources)
        {
            for(PhysicalRegister scratch:
                scratch_registers[static_cast<size_t>(register_class)])
            {
                if(!future_sources.contains(PhysicalLocation::reg(scratch)))
                {
                    return scratch;
                }
            }
            return std::nullopt;
        }

        OrderedMoveSource
        remap_source(OrderedMoveSource source,
                     const std::vector<size_t> &legalized_move_by_ordered_move)
        {
            switch(source.kind())
            {
                case OrderedMoveSource::Kind::OriginalAssignment:
                    return source;
                case OrderedMoveSource::Kind::Move:
                    return OrderedMoveSource::move(
                        legalized_move_by_ordered_move[source.index()]);
            }
        }

        Result<OrderedParallelAssignment<PhysicalLocation>,
               RegisterAllocationError>
        legalize_stack_transfers(
            OrderedParallelAssignment<PhysicalLocation> ordered,
            const ScratchRegisters &scratch_registers)
        {
            OrderedParallelAssignment<PhysicalLocation> result;
            result.aliasing_assignments =
                std::move(ordered.aliasing_assignments);

            FutureSourceCounts future_sources;
            for(const OrderedMove<PhysicalLocation> &move: ordered.moves)
            {
                ++future_sources[move.source_location];
            }

            std::vector<size_t> legalized_move_by_ordered_move;
            legalized_move_by_ordered_move.reserve(ordered.moves.size());
            for(const OrderedMove<PhysicalLocation> &move: ordered.moves)
            {
                OrderedMoveSource source =
                    remap_source(move.source, legalized_move_by_ordered_move);
                size_t completed_move = 0;
                if(move.source_location.is_stack() &&
                   move.destination.is_stack())
                {
                    std::optional<PhysicalRegister> scratch = available_scratch(
                        scratch_registers, move.register_class, future_sources);
                    if(!scratch.has_value())
                    {
                        return Result<
                            OrderedParallelAssignment<PhysicalLocation>,
                            RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      InsufficientTransferScratchRegisters);
                    }
                    PhysicalLocation scratch_location =
                        PhysicalLocation::reg(*scratch);
                    size_t scratch_move =
                        append_move(result, source, move.source_location,
                                    scratch_location, move.register_class, -1);
                    completed_move = append_move(
                        result, OrderedMoveSource::move(scratch_move),
                        scratch_location, move.destination, move.register_class,
                        move.original_assignment_index);
                }
                else
                {
                    completed_move = append_move(
                        result, source, move.source_location, move.destination,
                        move.register_class, move.original_assignment_index);
                }
                legalized_move_by_ordered_move.push_back(completed_move);

                auto future = future_sources.find(move.source_location);
                if(--future->second == 0)
                {
                    future_sources.erase(future);
                }
            }
            return Result<OrderedParallelAssignment<PhysicalLocation>,
                          RegisterAllocationError>::ok(std::move(result));
        }

        Result<OrderedParallelAssignment<PhysicalLocation>,
               RegisterAllocationError>
        plan_physical_assignments(
            std::span<const ParallelAssignment<PhysicalLocation>> assignments,
            const ScratchRegisters &scratch_registers)
        {
            auto ordered = order_parallel_assignments<PhysicalLocation>(
                assignments,
                [&](RegisterClass register_class,
                    size_t) -> std::optional<PhysicalLocation> {
                    const std::vector<PhysicalRegister> &scratch =
                        scratch_registers[static_cast<size_t>(register_class)];
                    if(scratch.empty())
                    {
                        return std::nullopt;
                    }
                    return PhysicalLocation::reg(scratch.front());
                });
            if(!ordered)
            {
                return Result<OrderedParallelAssignment<PhysicalLocation>,
                              RegisterAllocationError>::
                    error(RegisterAllocationError::
                              InsufficientTransferScratchRegisters);
            }
            return legalize_stack_transfers(std::move(ordered).value(),
                                            scratch_registers);
        }

        Result<MaterializationPlan, RegisterAllocationError>
        plan_materialization(const PreparedAllocationProblem &problem,
                             const AllocationConstraints &constraints,
                             const RegisterAllocationResult &allocation,
                             const BundleLocationAssignments &locations,
                             CompilationStorage &storage)
        {
            MaterializationPlan result;
            std::vector<std::vector<std::pair<LivenessRange, BundleId>>>
                fragments_by_live_range(problem.live_ranges().size());
            for(size_t index = 0; index < allocation.bundles().size(); ++index)
            {
                for(const BundleFragment &fragment:
                    allocation.bundles()[index].fragments)
                {
                    fragments_by_live_range[fragment.source.value()].push_back(
                        {fragment.range,
                         BundleId(static_cast<uint32_t>(index))});
                }
            }
            for(auto &fragments: fragments_by_live_range)
            {
                std::ranges::sort(fragments, {}, [](const auto &fragment) {
                    return fragment.first.start;
                });
            }

            std::vector<BundleId> bundle_by_occurrence;
            bundle_by_occurrence.reserve(problem.occurrences().size());
            for(const Occurrence &occurrence: problem.occurrences())
            {
                const auto &fragments =
                    fragments_by_live_range[occurrence.live_range.value()];
                auto found = std::ranges::upper_bound(
                    fragments, occurrence.minimum_coverage.start, {},
                    [](const auto &fragment) { return fragment.first.start; });
                if(found == fragments.begin())
                {
                    fatal("JIT occurrence is covered by no materialized "
                          "bundle");
                }
                --found;
                if(!found->first.contains(occurrence.minimum_coverage))
                {
                    fatal("JIT occurrence is covered by no materialized "
                          "bundle");
                }
                bundle_by_occurrence.push_back(found->second);
            }
            absl::flat_hash_map<const BlockEdge *,
                                std::vector<const BundleAffinity *>>
                affinities_by_edge;
            std::vector<const BlockEdge *> edge_order;
            for(const BundleAffinity &affinity: problem.bundle_affinities())
            {
                if(affinity.kind != BundleAffinityKind::BlockEdge)
                {
                    continue;
                }
                auto [position, inserted] = affinities_by_edge.try_emplace(
                    affinity.edge, std::vector<const BundleAffinity *>{});
                if(inserted)
                {
                    edge_order.push_back(affinity.edge);
                }
                position->second.push_back(&affinity);
            }

            ScratchRegisters scratch_registers;
            for(const RegisterClassDefinition &definition:
                constraints.register_classes())
            {
                scratch_registers[static_cast<size_t>(
                    definition.register_class())] =
                    definition.scratch_registers();
            }

            struct PendingFixedOperandCopySet
            {
                InstructionId instruction;
                std::vector<uint32_t> operand_indices;
                std::vector<BundleId> source_bundles;
                std::vector<ParallelAssignment<PhysicalLocation>> assignments;
                bool combined_with_transfers = false;
            };
            std::vector<PendingFixedOperandCopySet>
                pending_fixed_operand_copies;
            absl::flat_hash_map<InstructionId, size_t>
                pending_fixed_operand_copy_index;
            for(const FixedOperandCopyFixup &fixed_operand_copy:
                allocation.fixed_operand_copies())
            {
                if(fixed_operand_copy.source.value() >=
                   problem.occurrences().size())
                {
                    fatal("materialization fixed operand copy names no "
                          "occurrence");
                }
                const Occurrence &source =
                    problem.occurrences()[fixed_operand_copy.source.value()];
                if(source.anchor.kind() !=
                   OccurrenceAnchor::Kind::InstructionOperand)
                {
                    fatal("materialization fixed operand copy has no "
                          "instruction operand");
                }
                InstructionId instruction = source.anchor.instruction_id();
                auto [position, inserted] =
                    pending_fixed_operand_copy_index.emplace(
                        instruction, pending_fixed_operand_copies.size());
                if(inserted)
                {
                    pending_fixed_operand_copies.push_back(
                        {instruction, {}, {}, {}});
                }
                PendingFixedOperandCopySet &pending =
                    pending_fixed_operand_copies[position->second];
                BundleId source_bundle =
                    bundle_by_occurrence[fixed_operand_copy.source.value()];
                pending.operand_indices.push_back(
                    static_cast<uint32_t>(source.anchor.index()));
                pending.source_bundles.push_back(source_bundle);
                pending.assignments.push_back(
                    {locations.physical_location_for(source_bundle),
                     PhysicalLocation::reg(fixed_operand_copy.destination),
                     allocation.bundles()[source_bundle.value()]
                         .register_class});
            }
            std::optional<MaterializationPointIndex> point_index;
            std::optional<TransferSourceIndex> source_index;
            absl::flat_hash_map<const Block *, const BundleTransferSet *>
                block_exit_transfers;
            absl::flat_hash_map<const BlockEdge *, const BundleTransferSet *>
                block_edge_transfers;
            for(const BundleTransferSet &set: allocation.transfers().sets())
            {
                if(set.point.kind() == TransferPoint::Kind::BlockExit)
                {
                    bool inserted =
                        block_exit_transfers.emplace(set.point.block(), &set)
                            .second;
                    if(!inserted)
                    {
                        fatal("duplicate JIT block-exit transfer point");
                    }
                    continue;
                }
                if(set.point.kind() == TransferPoint::Kind::BlockEdge)
                {
                    bool inserted =
                        block_edge_transfers.emplace(set.point.edge(), &set)
                            .second;
                    if(!inserted)
                    {
                        fatal("duplicate JIT block-edge transfer point");
                    }
                    continue;
                }

                std::vector<ParallelAssignment<PhysicalLocation>> transfers;
                transfers.reserve(set.transfers.size());
                for(const BundleTransfer &transfer: set.transfers)
                {
                    if(transfer.source.value() >= allocation.bundles().size() ||
                       transfer.destination.value() >=
                           allocation.bundles().size())
                    {
                        fatal("materialization transfer names no bundle");
                    }
                    RegisterClass register_class =
                        allocation.bundles()[transfer.source.value()]
                            .register_class;
                    if(allocation.bundles()[transfer.destination.value()]
                           .register_class != register_class)
                    {
                        fatal("materialization transfer crosses register "
                              "classes");
                    }
                    transfers.push_back(
                        {locations.physical_location_for(transfer.source),
                         locations.physical_location_for(transfer.destination),
                         register_class});
                }

                std::vector<uint32_t> fixed_operand_indices;
                if(set.point.kind() == TransferPoint::Kind::BeforeInstruction)
                {
                    auto fixed = pending_fixed_operand_copy_index.find(
                        set.point.instruction_id());
                    if(fixed != pending_fixed_operand_copy_index.end())
                    {
                        PendingFixedOperandCopySet &pending =
                            pending_fixed_operand_copies[fixed->second];
                        assert(!pending.combined_with_transfers);
                        std::vector<ParallelAssignment<PhysicalLocation>>
                            incoming_assignments;
                        incoming_assignments.reserve(
                            pending.assignments.size());
                        bool all_sources_are_registers = true;
                        for(size_t index = 0;
                            index < pending.assignments.size(); ++index)
                        {
                            ParallelAssignment<PhysicalLocation> assignment =
                                pending.assignments[index];
                            BundleId source_bundle =
                                pending.source_bundles[index];
                            for(const BundleTransfer &transfer: set.transfers)
                            {
                                if(transfer.destination == source_bundle)
                                {
                                    assignment.source =
                                        locations.physical_location_for(
                                            transfer.source);
                                    break;
                                }
                            }
                            all_sources_are_registers &=
                                assignment.source.is_register();
                            incoming_assignments.push_back(assignment);
                        }
                        if(all_sources_are_registers)
                        {
                            pending.combined_with_transfers = true;
                            fixed_operand_indices = pending.operand_indices;
                            transfers.insert(transfers.end(),
                                             incoming_assignments.begin(),
                                             incoming_assignments.end());
                        }
                    }
                }
                auto ordered =
                    plan_physical_assignments(transfers, scratch_registers);
                if(!ordered)
                {
                    return propagate_failure(std::move(ordered));
                }
                std::vector<InstructionId> sources;
                if(needs_explicit_transfer_sources(set.point) &&
                   !set.transfers.empty())
                {
                    if(!point_index.has_value())
                    {
                        point_index =
                            build_materialization_point_index(problem);
                    }
                    if(!source_index.has_value())
                    {
                        source_index =
                            build_transfer_source_index(problem, allocation);
                    }
                    sources = transfer_sources_at_point(set, *point_index,
                                                        *source_index);
                }
                PlannedTransferSet planned{
                    set.transfers, std::move(ordered).value(),
                    std::move(sources), std::move(fixed_operand_indices)};

                bool inserted = false;
                switch(set.point.kind())
                {
                    case TransferPoint::Kind::BlockEntry:
                        inserted =
                            result.block_entries
                                .emplace(set.point.block(), std::move(planned))
                                .second;
                        break;
                    case TransferPoint::Kind::BeforeInstruction:
                        inserted = result.before_instructions
                                       .emplace(set.point.instruction_id(),
                                                std::move(planned))
                                       .second;
                        break;
                    case TransferPoint::Kind::BlockExit:
                    case TransferPoint::Kind::BlockEdge:
                        fatal("edge transfer reached ordinary materialization "
                              "planning");
                }
                if(!inserted)
                {
                    fatal("duplicate JIT materialization transfer point");
                }
            }

            absl::flat_hash_map<const BundleTransferSet *, std::vector<bool>>
                used_block_exit_transfers;
            for(const auto &[block, set]: block_exit_transfers)
            {
                (void)block;
                used_block_exit_transfers.emplace(
                    set, std::vector<bool>(set->transfers.size(), false));
            }

            auto plan_transfer_phase =
                [&](std::vector<BundleTransfer> phase_transfers)
                -> Result<PlannedTransferSet, RegisterAllocationError> {
                std::vector<ParallelAssignment<PhysicalLocation>> assignments;
                assignments.reserve(phase_transfers.size());
                for(const BundleTransfer &transfer: phase_transfers)
                {
                    if(transfer.source.value() >= allocation.bundles().size() ||
                       transfer.destination.value() >=
                           allocation.bundles().size())
                    {
                        fatal("materialization transfer names no bundle");
                    }
                    RegisterClass register_class =
                        allocation.bundles()[transfer.source.value()]
                            .register_class;
                    if(allocation.bundles()[transfer.destination.value()]
                           .register_class != register_class)
                    {
                        fatal("materialization transfer crosses register "
                              "classes");
                    }
                    assignments.push_back(
                        {locations.physical_location_for(transfer.source),
                         locations.physical_location_for(transfer.destination),
                         register_class});
                }
                auto ordered =
                    plan_physical_assignments(assignments, scratch_registers);
                if(!ordered)
                {
                    return propagate_failure(std::move(ordered));
                }
                return Result<PlannedTransferSet, RegisterAllocationError>::ok(
                    {std::move(phase_transfers),
                     std::move(ordered).value(),
                     {},
                     {}});
            };

            for(const BlockEdge *edge: edge_order)
            {
                const std::vector<const BundleAffinity *> &affinities =
                    affinities_by_edge.at(edge);
                auto block_exit = block_exit_transfers.find(edge->source());
                const BundleTransferSet *exit_set =
                    block_exit == block_exit_transfers.end()
                        ? nullptr
                        : block_exit->second;
                const BundleTransferSet *edge_set = nullptr;
                auto existing_edge = block_edge_transfers.find(edge);
                if(existing_edge != block_edge_transfers.end())
                {
                    edge_set = existing_edge->second;
                }

                std::vector<std::optional<size_t>> exit_transfer_by_argument(
                    edge->arguments().size());
                bool has_block_exit_transfer = false;
                if(exit_set != nullptr)
                {
                    for(const BundleAffinity *affinity: affinities)
                    {
                        BundleId source =
                            bundle_by_occurrence[affinity->source.value()];
                        for(size_t index = 0;
                            index < exit_set->transfers.size(); ++index)
                        {
                            if(exit_set->transfers[index].destination == source)
                            {
                                exit_transfer_by_argument
                                    [affinity->argument_index] = index;
                                used_block_exit_transfers.at(exit_set)[index] =
                                    true;
                                has_block_exit_transfer = true;
                                break;
                            }
                        }
                    }
                }
                if(!has_block_exit_transfer && edge_set == nullptr)
                {
                    continue;
                }

                std::vector<BundleTransfer> exit_phase;
                if(has_block_exit_transfer)
                {
                    const std::vector<bool> &used =
                        used_block_exit_transfers.at(exit_set);
                    for(size_t index = 0; index < used.size(); ++index)
                    {
                        bool used_on_edge = false;
                        for(std::optional<size_t> argument_transfer:
                            exit_transfer_by_argument)
                        {
                            used_on_edge |= argument_transfer == index;
                        }
                        if(used_on_edge)
                        {
                            exit_phase.push_back(exit_set->transfers[index]);
                        }
                    }
                }

                std::vector<BundleTransfer> edge_phase;
                edge_phase.reserve(affinities.size());
                std::vector<std::optional<PhysicalLocation>>
                    parameter_locations(edge->arguments().size());
                std::vector<std::optional<BundleId>> parameter_bundles(
                    edge->arguments().size());
                std::vector<std::optional<BundleId>> outgoing_bundles(
                    edge->arguments().size());
                [[maybe_unused]] size_t scheduled_edge_transfer = 0;
                for(const BundleAffinity *affinity: affinities)
                {
                    uint32_t argument_index = affinity->argument_index;
                    BundleId source =
                        bundle_by_occurrence[affinity->source.value()];
                    BundleId destination =
                        bundle_by_occurrence[affinity->destination.value()];
                    BundleId parameter_bundle = source;
                    if(exit_transfer_by_argument[argument_index].has_value())
                    {
                        parameter_bundle =
                            exit_set
                                ->transfers
                                    [*exit_transfer_by_argument[argument_index]]
                                .source;
                    }
                    assert(!parameter_locations[argument_index].has_value());
                    parameter_locations[argument_index] =
                        locations.physical_location_for(parameter_bundle);
                    parameter_bundles[argument_index] = parameter_bundle;
                    outgoing_bundles[argument_index] = destination;
                    edge_phase.push_back({source, destination});

                    PhysicalLocation source_location =
                        locations.physical_location_for(source);
                    if(source != destination &&
                       !source_location.aliases(
                           locations.physical_location_for(destination)))
                    {
                        assert(edge_set != nullptr);
                        assert(scheduled_edge_transfer <
                               edge_set->transfers.size());
                        assert(edge_set->transfers[scheduled_edge_transfer]
                                   .source == source);
                        assert(edge_set->transfers[scheduled_edge_transfer]
                                   .destination == destination);
                        ++scheduled_edge_transfer;
                    }
                }
                assert(edge_set == nullptr ||
                       scheduled_edge_transfer == edge_set->transfers.size());

                PlannedEdgeTransferSequence sequence;
                sequence.edge = const_cast<BlockEdge *>(edge);
                if(!exit_phase.empty())
                {
                    auto planned = plan_transfer_phase(std::move(exit_phase));
                    if(!planned)
                    {
                        return propagate_failure(std::move(planned));
                    }
                    sequence.phases.push_back(std::move(planned).value());
                }
                auto planned_edge = plan_transfer_phase(std::move(edge_phase));
                if(!planned_edge)
                {
                    return propagate_failure(std::move(planned_edge));
                }
                sequence.phases.push_back(std::move(planned_edge).value());
                sequence.parameter_locations.reserve(
                    parameter_locations.size());
                sequence.parameter_bundles.reserve(parameter_bundles.size());
                sequence.outgoing_bundles.reserve(outgoing_bundles.size());
                for(size_t index = 0; index < parameter_locations.size();
                    ++index)
                {
                    assert(parameter_locations[index].has_value());
                    assert(parameter_bundles[index].has_value());
                    assert(outgoing_bundles[index].has_value());
                    sequence.parameter_locations.push_back(
                        *parameter_locations[index]);
                    sequence.parameter_bundles.push_back(
                        *parameter_bundles[index]);
                    sequence.outgoing_bundles.push_back(
                        *outgoing_bundles[index]);
                }
                result.edge_transfers.push_back(std::move(sequence));
            }

            for(const auto &[set, used]: used_block_exit_transfers)
            {
                (void)set;
                if(std::ranges::find(used, false) != used.end())
                {
                    fatal("JIT block-exit transfer reaches no outgoing edge");
                }
            }

            for(PendingFixedOperandCopySet &pending:
                pending_fixed_operand_copies)
            {
                if(pending.combined_with_transfers)
                {
                    continue;
                }
                auto ordered = plan_physical_assignments(pending.assignments,
                                                         scratch_registers);
                if(!ordered)
                {
                    return propagate_failure(std::move(ordered));
                }
                bool inserted =
                    result.fixed_operand_copies
                        .emplace(pending.instruction,
                                 PlannedFixedOperandCopySet{
                                     std::move(pending.operand_indices),
                                     std::move(ordered).value()})
                        .second;
                assert(inserted);
                (void)inserted;
            }

            for(size_t index = 0; index < problem.occurrences().size(); ++index)
            {
                const Occurrence &occurrence = problem.occurrences()[index];
                BundleId bundle = bundle_by_occurrence[index];
                PhysicalLocation location =
                    locations.physical_location_for(bundle);
                switch(occurrence.anchor.kind())
                {
                    case OccurrenceAnchor::Kind::InstructionResult:
                        {
                            result.existing_locations.assign(
                                ProgramValueRef(storage.instruction(
                                    occurrence.anchor.instruction_id())),
                                location);
                            break;
                        }
                    case OccurrenceAnchor::Kind::InstructionTemporary:
                        result.existing_locations.assign(
                            storage.instruction(
                                occurrence.anchor.instruction_id()),
                            occurrence.anchor.index(), location);
                        break;
                    case OccurrenceAnchor::Kind::InstructionOperand:
                    case OccurrenceAnchor::Kind::BlockEdgeArgument:
                        break;
                }
            }

            result.initial_value_by_bundle.resize(allocation.bundles().size());
            for(size_t index = 0; index < allocation.bundles().size(); ++index)
            {
                std::optional<InstructionId> value;
                bool has_multiple_values = false;
                for(const BundleFragment &fragment:
                    allocation.bundles()[index].fragments)
                {
                    const LiveRange &source =
                        problem.live_ranges()[fragment.source.value()];
                    if(source.origin.kind() !=
                       LiveRangeOrigin::Kind::ProgramValue)
                    {
                        continue;
                    }
                    InstructionId candidate =
                        source.origin.program_value().instruction_id();
                    if(value.has_value() && *value != candidate)
                    {
                        has_multiple_values = true;
                        break;
                    }
                    value = candidate;
                }
                if(!has_multiple_values)
                {
                    result.initial_value_by_bundle[index] = value;
                }
            }

            return Result<MaterializationPlan, RegisterAllocationError>::ok(
                std::move(result));
        }

        EdgeSplitPlacement edge_split_placement(const ControlFlowGraph &graph,
                                                const BlockEdge &edge)
        {
            if(edge.target() == graph.entry_block() ||
               edge.source()->block_successor_edges().size() == 1)
            {
                return EdgeSplitPlacement::AfterSource;
            }
            return EdgeSplitPlacement::BeforeTarget;
        }

        void stage_edge_transfers(MaterializationPlan &plan,
                                  GraphRewriter &rewriter,
                                  const ControlFlowGraph &graph)
        {
            std::vector<EdgeSplitRequest> requests;
            requests.reserve(plan.edge_transfers.size());
            for(const PlannedEdgeTransferSequence &edge: plan.edge_transfers)
            {
                requests.push_back(
                    {edge.edge, edge_split_placement(graph, *edge.edge)});
            }
            std::vector<Block *> blocks = rewriter.stage_edge_splits(requests);
            assert(blocks.size() == plan.edge_transfers.size());

            for(size_t index = 0; index < blocks.size(); ++index)
            {
                Block *block = blocks[index];
                PlannedEdgeTransferSequence &edge = plan.edge_transfers[index];
                assert(block->parameters().size() ==
                       edge.parameter_locations.size());
                for(size_t parameter_index = 0;
                    parameter_index < block->parameters().size();
                    ++parameter_index)
                {
                    plan.existing_locations.assign(
                        ProgramValueRef(block->parameter_at(parameter_index)),
                        edge.parameter_locations[parameter_index]);
                }

                bool inserted =
                    plan.edge_entries
                        .emplace(block,
                                 PlannedEdgeTransferEntry{
                                     std::move(edge.phases),
                                     std::move(edge.parameter_bundles),
                                     std::move(edge.outgoing_bundles)})
                        .second;
                assert(inserted);
                (void)inserted;
            }
            plan.edge_transfers.clear();
        }

        class AllocationMaterializer
        {
        public:
            explicit AllocationMaterializer(MaterializationPlan plan)
                : block_entries_(std::move(plan.block_entries)),
                  before_instructions_(std::move(plan.before_instructions)),
                  fixed_operand_copies_(std::move(plan.fixed_operand_copies)),
                  edge_entries_(std::move(plan.edge_entries)),
                  locations_(std::move(plan.existing_locations)),
                  current_values_(std::move(plan.initial_value_by_bundle))
            {
            }

            RewriteInsertion at_block_entry(RewriteContext &context,
                                            const GraphQueries &,
                                            const Block &block)
            {
                auto edge = edge_entries_.find(&block);
                if(edge != edge_entries_.end())
                {
                    return emit_edge_transfers(context, block, edge->second);
                }
                auto found = block_entries_.find(&block);
                return found == block_entries_.end()
                           ? RewriteInsertion::none()
                           : emit_transfers(context, found->second);
            }

            RewriteResult rewrite_instruction(RewriteContext &context,
                                              const GraphQueries &,
                                              const Block &,
                                              const Instruction &instruction)
            {
                if(pending_fixed_operand_replacements_.has_value())
                {
                    PendingFixedOperandReplacements replacements =
                        std::move(*pending_fixed_operand_replacements_);
                    pending_fixed_operand_replacements_.reset();
                    return replace_fixed_operands(
                        context, instruction, replacements.operand_indices,
                        replacements.replacements, {});
                }
                const PlannedFixedOperandCopySet *fixed_operand_copies =
                    pending_fixed_operand_copies_;
                pending_fixed_operand_copies_ = nullptr;
                if(fixed_operand_copies == nullptr)
                {
                    return RewriteResult::keep();
                }
                return emit_fixed_operand_copies(context, instruction,
                                                 *fixed_operand_copies);
            }

            RewriteInsertion before_instruction(RewriteContext &context,
                                                const GraphQueries &,
                                                const Block &,
                                                const Instruction &instruction)
            {
                assert(pending_fixed_operand_copies_ == nullptr);
                assert(!pending_fixed_operand_replacements_.has_value());
                auto transfers = before_instructions_.find(instruction.id());
                if(transfers != before_instructions_.end() &&
                   !transfers->second.fixed_operand_indices.empty())
                {
                    return emit_instruction_transfers(context, instruction,
                                                      transfers->second);
                }
                auto fixed_operand_copy =
                    fixed_operand_copies_.find(instruction.id());
                pending_fixed_operand_copies_ =
                    fixed_operand_copy == fixed_operand_copies_.end()
                        ? nullptr
                        : &fixed_operand_copy->second;
                return transfers == before_instructions_.end()
                           ? RewriteInsertion::none()
                           : emit_transfers(context, transfers->second);
            }

            LocationAssignments
            finish(const NormalizationRemapping &normalization) &&
            {
                assert(pending_fixed_operand_copies_ == nullptr);
                assert(!pending_fixed_operand_replacements_.has_value());
                NormalizationRemapping location_remapping = normalization;
                for(const auto &[before, after]: materialization_replacements_)
                {
                    bool inserted =
                        location_remapping.emplace(before, after).second;
                    assert(inserted);
                    (void)inserted;
                }
                return std::move(locations_).finalize(location_remapping);
            }

        private:
            struct EmittedParallelAssignment
            {
                RewriteInsertion::InstructionSequence instructions;
                std::vector<InstructionId> result_by_assignment;
                std::vector<size_t> moved_assignments;
            };

            class OperandReplacementResolver
            {
            public:
                explicit OperandReplacementResolver(
                    const absl::flat_hash_map<uint32_t, InstructionId>
                        &replacements)
                    : replacements_(&replacements)
                {
                }

                InstructionId resolve(uint32_t operand_index,
                                      InstructionId definition) const
                {
                    auto found = replacements_->find(operand_index);
                    return found == replacements_->end() ? definition
                                                         : found->second;
                }

                InstructionId resolve(InstructionId definition) const
                {
                    return definition;
                }

                BlockEdge *resolve(BlockEdge *edge) const { return edge; }

            private:
                const absl::flat_hash_map<uint32_t, InstructionId>
                    *replacements_;
            };

            EmittedParallelAssignment emit_parallel_assignment(
                RewriteContext &context,
                const OrderedParallelAssignment<PhysicalLocation> &ordered,
                std::span<const InstructionId> sources)
            {
                EmittedParallelAssignment result;
                result.result_by_assignment.assign(sources.begin(),
                                                   sources.end());
                std::vector<InstructionId> move_values;
                move_values.reserve(ordered.moves.size());
                for(const OrderedMove<PhysicalLocation> &move: ordered.moves)
                {
                    InstructionId source(0);
                    switch(move.source.kind())
                    {
                        case OrderedMoveSource::Kind::OriginalAssignment:
                            source = sources[move.source.index()];
                            break;
                        case OrderedMoveSource::Kind::Move:
                            source = move_values[move.source.index()];
                            break;
                    }

                    Instruction source_instruction =
                        context.instruction(source);
                    std::optional<Instruction> output;
                    switch(source_instruction.value_representation())
                    {
                        case ValueRepresentation::TaggedValue:
                            if(move.source_location.is_register() &&
                               move.destination.is_register())
                            {
                                output =
                                    context.make_instruction<MovInstruction>(
                                        TaggedValueRef(source_instruction));
                            }
                            else if(move.source_location.is_stack() &&
                                    move.destination.is_register())
                            {
                                output =
                                    context
                                        .make_instruction<LoadStackInstruction>(
                                            TaggedValueRef(source_instruction));
                            }
                            else if(move.source_location.is_register() &&
                                    move.destination.is_stack())
                            {
                                output = context.make_instruction<
                                    StoreStackInstruction>(
                                    TaggedValueRef(source_instruction));
                            }
                            break;
                        case ValueRepresentation::F64:
                            if(move.source_location.is_register() &&
                               move.destination.is_register())
                            {
                                output =
                                    context.make_instruction<MovF64Instruction>(
                                        F64Ref(source_instruction));
                            }
                            else if(move.source_location.is_stack() &&
                                    move.destination.is_register())
                            {
                                output = context.make_instruction<
                                    LoadStackF64Instruction>(
                                    F64Ref(source_instruction));
                            }
                            else if(move.source_location.is_register() &&
                                    move.destination.is_stack())
                            {
                                output = context.make_instruction<
                                    StoreStackF64Instruction>(
                                    F64Ref(source_instruction));
                            }
                            break;
                        case ValueRepresentation::Pointer:
                            if(move.source_location.is_register() &&
                               move.destination.is_register())
                            {
                                output = context.make_instruction<
                                    MovPointerInstruction>(
                                    PointerRef(source_instruction));
                            }
                            else if(move.source_location.is_stack() &&
                                    move.destination.is_register())
                            {
                                output = context.make_instruction<
                                    LoadStackPointerInstruction>(
                                    PointerRef(source_instruction));
                            }
                            else if(move.source_location.is_register() &&
                                    move.destination.is_stack())
                            {
                                output = context.make_instruction<
                                    StoreStackPointerInstruction>(
                                    PointerRef(source_instruction));
                            }
                            break;
                        case ValueRepresentation::None:
                        case ValueRepresentation::Count:
                            fatal("invalid JIT bundle transfer representation");
                    }
                    if(!output.has_value())
                    {
                        fatal("invalid resolved JIT transfer locations");
                    }
                    result.instructions.push_back(*output);
                    move_values.push_back(output->id());
                    locations_.assign(ProgramValueRef(*output),
                                      move.destination);

                    if(move.original_assignment_index >= 0)
                    {
                        size_t assignment =
                            static_cast<size_t>(move.original_assignment_index);
                        result.result_by_assignment[assignment] = output->id();
                        result.moved_assignments.push_back(assignment);
                    }
                }
                return result;
            }

            RewriteInsertion emit_transfers(RewriteContext &context,
                                            const PlannedTransferSet &planned)
            {
                bool uses_explicit_sources = !planned.sources.empty();
                std::span<const InstructionId> sources = planned.sources;
                std::vector<InstructionId> current_sources;
                if(!uses_explicit_sources)
                {
                    current_sources.reserve(planned.transfers.size());
                    for(const BundleTransfer &transfer: planned.transfers)
                    {
                        std::optional<InstructionId> source =
                            current_values_[transfer.source.value()];
                        if(!source.has_value())
                        {
                            fatal("JIT bundle transfer has no program value");
                        }
                        current_sources.push_back(*source);
                    }
                    sources = current_sources;
                }
                assert(sources.size() == planned.transfers.size());
                EmittedParallelAssignment emitted =
                    emit_parallel_assignment(context, planned.ordered, sources);
                RewriteInsertion::TransferOutputs outputs;
                if(!uses_explicit_sources)
                {
                    for(size_t index = 0; index < planned.transfers.size();
                        ++index)
                    {
                        current_values_[planned.transfers[index]
                                            .destination.value()] =
                            emitted.result_by_assignment[index];
                    }
                }

                for(size_t index: emitted.moved_assignments)
                {
                    outputs.emplace_back(
                        ProgramValueRef(context.instruction(sources[index])),
                        ProgramValueRef(context.instruction(
                            emitted.result_by_assignment[index])));
                }

                if(emitted.instructions.empty())
                {
                    return RewriteInsertion::none();
                }

                return RewriteInsertion::insert_transfers(
                    std::move(emitted.instructions), std::move(outputs));
            }

            RewriteInsertion
            emit_edge_transfers(RewriteContext &context, const Block &block,
                                const PlannedEdgeTransferEntry &planned)
            {
                assert(block.parameters().size() ==
                       planned.parameter_bundles.size());
                assert(block.parameters().size() ==
                       planned.outgoing_bundles.size());

                absl::flat_hash_map<BundleId, InstructionId> values;
                values.reserve(planned.parameter_bundles.size());
                for(size_t index = 0; index < block.parameters().size();
                    ++index)
                {
                    values.try_emplace(planned.parameter_bundles[index],
                                       block.parameter_at(index).id());
                }

                RewriteInsertion::InstructionSequence instructions;
                for(const PlannedTransferSet &phase: planned.phases)
                {
                    std::vector<InstructionId> sources;
                    sources.reserve(phase.transfers.size());
                    for(const BundleTransfer &transfer: phase.transfers)
                    {
                        auto source = values.find(transfer.source);
                        if(source == values.end())
                        {
                            fatal("JIT edge transfer has no source value");
                        }
                        sources.push_back(source->second);
                    }
                    EmittedParallelAssignment emitted =
                        emit_parallel_assignment(context, phase.ordered,
                                                 sources);
                    instructions.insert(instructions.end(),
                                        emitted.instructions.begin(),
                                        emitted.instructions.end());
                    for(size_t index = 0; index < phase.transfers.size();
                        ++index)
                    {
                        values.insert_or_assign(
                            phase.transfers[index].destination,
                            emitted.result_by_assignment[index]);
                    }
                }

                RewriteInsertion::TransferOutputs outputs;
                for(size_t index = 0; index < block.parameters().size();
                    ++index)
                {
                    Instruction parameter = block.parameter_at(index);
                    auto output = values.find(planned.outgoing_bundles[index]);
                    if(output == values.end())
                    {
                        fatal("JIT edge transfer has no outgoing value");
                    }
                    if(output->second != parameter.id())
                    {
                        outputs.emplace_back(
                            ProgramValueRef(parameter),
                            ProgramValueRef(
                                context.instruction(output->second)));
                    }
                }
                return RewriteInsertion::insert_transfers(
                    std::move(instructions), std::move(outputs));
            }

            std::vector<InstructionId>
            fixed_operand_sources(const Instruction &instruction,
                                  std::span<const uint32_t> operand_indices)
            {
                absl::flat_hash_map<uint32_t, size_t> use_by_operand;
                use_by_operand.reserve(operand_indices.size());
                for(size_t index = 0; index < operand_indices.size(); ++index)
                {
                    bool inserted =
                        use_by_operand.emplace(operand_indices[index], index)
                            .second;
                    if(!inserted)
                    {
                        fatal("JIT instruction has two fixed operand copies "
                              "for one operand");
                    }
                }

                std::vector<InstructionId> sources(operand_indices.size(),
                                                   InstructionId(0));
                std::vector<bool> found_source(operand_indices.size(), false);
                visit_operand_references(
                    instruction,
                    [&](uint32_t operand_index, OperandClass operand_class,
                        ValueRepresentationRequirement,
                        InstructionId definition) {
                        auto found = use_by_operand.find(operand_index);
                        if(found == use_by_operand.end())
                        {
                            return;
                        }
                        if(operand_class != OperandClass::ProgramValue)
                        {
                            fatal("JIT fixed operand copy does not name a "
                                  "program value");
                        }
                        sources[found->second] = definition;
                        found_source[found->second] = true;
                    });
                if(std::ranges::find(found_source, false) != found_source.end())
                {
                    fatal("JIT fixed operand copy names no materialized "
                          "operand");
                }
                return sources;
            }

            RewriteInsertion
            emit_instruction_transfers(RewriteContext &context,
                                       const Instruction &instruction,
                                       const PlannedTransferSet &planned)
            {
                size_t transfer_count = planned.transfers.size();
                assert(planned.sources.size() == transfer_count);

                std::vector<InstructionId> sources = planned.sources;
                std::vector<InstructionId> fixed_sources =
                    fixed_operand_sources(instruction,
                                          planned.fixed_operand_indices);
                sources.insert(sources.end(), fixed_sources.begin(),
                               fixed_sources.end());

                EmittedParallelAssignment emitted =
                    emit_parallel_assignment(context, planned.ordered, sources);
                RewriteInsertion::TransferOutputs outputs;
                for(size_t index: emitted.moved_assignments)
                {
                    if(index >= transfer_count)
                    {
                        continue;
                    }
                    outputs.emplace_back(
                        ProgramValueRef(context.instruction(sources[index])),
                        ProgramValueRef(context.instruction(
                            emitted.result_by_assignment[index])));
                }

                PendingFixedOperandReplacements replacements;
                replacements.operand_indices = planned.fixed_operand_indices;
                replacements.replacements.reserve(
                    planned.fixed_operand_indices.size());
                for(size_t index = 0;
                    index < planned.fixed_operand_indices.size(); ++index)
                {
                    replacements.replacements.push_back(
                        emitted.result_by_assignment[transfer_count + index]);
                }
                pending_fixed_operand_replacements_ = std::move(replacements);

                if(emitted.instructions.empty())
                {
                    return RewriteInsertion::none();
                }
                return RewriteInsertion::insert_transfers(
                    std::move(emitted.instructions), std::move(outputs));
            }

            RewriteResult replace_fixed_operands(
                RewriteContext &context, const Instruction &instruction,
                std::span<const uint32_t> operand_indices,
                std::span<const InstructionId> replacement_values,
                RewriteInsertion::InstructionSequence prefix)
            {
                assert(operand_indices.size() == replacement_values.size());
                absl::flat_hash_map<uint32_t, InstructionId> replacements;
                replacements.reserve(operand_indices.size());
                for(size_t index = 0; index < operand_indices.size(); ++index)
                {
                    bool inserted = replacements
                                        .emplace(operand_indices[index],
                                                 replacement_values[index])
                                        .second;
                    assert(inserted);
                    (void)inserted;
                }
                OperandReplacementResolver resolver(replacements);
                Instruction original = instruction;
                Instruction replacement = rebuild_instruction_with_references(
                    original, *instruction.storage(), resolver, context,
                    InstructionRebuildMode::AlwaysClone);
                prefix.push_back(replacement);
                if(replacement.result_class() != ResultClass::None)
                {
                    bool inserted =
                        materialization_replacements_
                            .emplace(original.id(), replacement.id())
                            .second;
                    assert(inserted);
                    (void)inserted;
                }
                switch(replacement.result_class())
                {
                    case ResultClass::None:
                        return RewriteResult::replace_without_result(
                            std::move(prefix));
                    case ResultClass::ProgramValue:
                        return RewriteResult::replace(
                            std::move(prefix), ProgramValueRef(replacement));
                    case ResultClass::Snapshot:
                        return RewriteResult::replace(std::move(prefix),
                                                      SnapshotRef(replacement));
                    case ResultClass::Count:
                        break;
                }
                fatal("invalid fixed-operand-copy instruction result class");
            }

            RewriteResult
            emit_fixed_operand_copies(RewriteContext &context,
                                      const Instruction &instruction,
                                      const PlannedFixedOperandCopySet &planned)
            {
                std::vector<InstructionId> sources =
                    fixed_operand_sources(instruction, planned.operand_indices);
                EmittedParallelAssignment emitted =
                    emit_parallel_assignment(context, planned.ordered, sources);
                if(emitted.instructions.empty())
                {
                    return RewriteResult::keep();
                }
                return replace_fixed_operands(context, instruction,
                                              planned.operand_indices,
                                              emitted.result_by_assignment,
                                              std::move(emitted.instructions));
            }

            absl::flat_hash_map<const Block *, PlannedTransferSet>
                block_entries_;
            absl::flat_hash_map<InstructionId, PlannedTransferSet>
                before_instructions_;
            absl::flat_hash_map<InstructionId, PlannedFixedOperandCopySet>
                fixed_operand_copies_;
            absl::flat_hash_map<const Block *, PlannedEdgeTransferEntry>
                edge_entries_;
            const PlannedFixedOperandCopySet *pending_fixed_operand_copies_ =
                nullptr;
            std::optional<PendingFixedOperandReplacements>
                pending_fixed_operand_replacements_;
            NormalizationRemapping materialization_replacements_;
            LocationAssignmentsBuilder locations_;
            std::vector<std::optional<InstructionId>> current_values_;
        };
    }  // namespace

    Result<MaterializedAllocation, RegisterAllocationError>
    materialize_allocation(CompilationSession &session, ControlFlowGraph &graph,
                           const PreparedAllocationProblem &problem,
                           const AllocationConstraints &constraints,
                           const RegisterAllocationResult &allocation)
    {
        auto locations_result = resolve_bundle_locations(graph, allocation);
        if(!locations_result)
        {
            return propagate_failure(std::move(locations_result));
        }
        ResolvedBundleLocations resolved = std::move(locations_result).value();
        auto plan_result =
            plan_materialization(problem, constraints, allocation,
                                 resolved.locations, *graph.storage());
        if(!plan_result)
        {
            return propagate_failure(std::move(plan_result));
        }

        MaterializationPlan plan = std::move(plan_result).value();
        GraphRewriter rewriter(session, graph);
        stage_edge_transfers(plan, rewriter, graph);
        AllocationMaterializer materializer(std::move(plan));
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(), RewriteInput::Normalized, materializer);
        return Result<MaterializedAllocation, RegisterAllocationError>::ok(
            MaterializedAllocation(
                std::move(materializer).finish(summary.normalization_remapping),
                resolved.managed_frame_spill_extent));
    }

}  // namespace cl::jit
