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
            const BundleTransferSet *original;
            OrderedParallelAssignment<PhysicalLocation> ordered;
            std::vector<InstructionId> sources;
        };

        struct PlannedEdgeTransferSet
        {
            BlockEdge *edge;
            PlannedTransferSet transfers;
            std::vector<uint32_t> argument_indices;
            std::vector<PhysicalLocation> parameter_locations;
        };

        struct PlannedFixedOperandCopySet
        {
            std::vector<uint32_t> operand_indices;
            OrderedParallelAssignment<PhysicalLocation> ordered;
        };

        struct MaterializationPlan
        {
            absl::flat_hash_map<const Block *, PlannedTransferSet>
                block_entries;
            absl::flat_hash_map<InstructionId, PlannedTransferSet>
                before_instructions;
            absl::flat_hash_map<InstructionId, PlannedFixedOperandCopySet>
                fixed_operand_copies;
            std::vector<PlannedEdgeTransferSet> edge_transfers;
            LocationAssignmentsBuilder existing_locations;
            std::vector<std::optional<InstructionId>> initial_value_by_bundle;
        };

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
            for(const BundleAffinity &affinity: problem.bundle_affinities())
            {
                if(affinity.kind != BundleAffinityKind::BlockEdge)
                {
                    continue;
                }
                affinities_by_edge[affinity.edge].push_back(&affinity);
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
                std::vector<ParallelAssignment<PhysicalLocation>> assignments;
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
                        {instruction, {}, {}});
                }
                PendingFixedOperandCopySet &pending =
                    pending_fixed_operand_copies[position->second];
                BundleId source_bundle =
                    bundle_by_occurrence[fixed_operand_copy.source.value()];
                pending.operand_indices.push_back(
                    static_cast<uint32_t>(source.anchor.index()));
                pending.assignments.push_back(
                    {allocation.locations().physical_location_for(
                         source_bundle),
                     PhysicalLocation::reg(fixed_operand_copy.destination),
                     allocation.bundles()[source_bundle.value()]
                         .register_class});
            }
            for(PendingFixedOperandCopySet &pending:
                pending_fixed_operand_copies)
            {
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

            std::optional<MaterializationPointIndex> point_index;
            std::optional<TransferSourceIndex> source_index;
            for(const BundleTransferSet &set: allocation.transfers().sets())
            {
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
                        {allocation.locations().physical_location_for(
                             transfer.source),
                         allocation.locations().physical_location_for(
                             transfer.destination),
                         register_class});
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
                PlannedTransferSet planned{&set, std::move(ordered).value(),
                                           std::move(sources)};

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
                        return Result<MaterializationPlan,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      UnsupportedTransferPoint);
                    case TransferPoint::Kind::BlockEdge:
                        {
                            const BlockEdge *edge = set.point.edge();
                            std::vector<std::optional<PhysicalLocation>>
                                parameter_locations(edge->arguments().size());
                            std::vector<uint32_t> argument_indices;
                            argument_indices.reserve(set.transfers.size());
                            size_t transfer_index = 0;
                            for(const BundleAffinity *affinity_pointer:
                                affinities_by_edge.at(edge))
                            {
                                const BundleAffinity &affinity =
                                    *affinity_pointer;
                                std::optional<PhysicalLocation> &location =
                                    parameter_locations[affinity
                                                            .argument_index];
                                assert(!location.has_value());
                                BundleId source =
                                    bundle_by_occurrence[affinity.source
                                                             .value()];
                                BundleId destination =
                                    bundle_by_occurrence[affinity.destination
                                                             .value()];
                                location = allocation.locations()
                                               .physical_location_for(source);
                                if(source != destination &&
                                   !location->aliases(
                                       allocation.locations()
                                           .physical_location_for(destination)))
                                {
                                    assert(transfer_index <
                                           set.transfers.size());
                                    const BundleTransfer &transfer =
                                        set.transfers[transfer_index++];
                                    assert(transfer.source == source);
                                    assert(transfer.destination == destination);
                                    (void)transfer;
                                    argument_indices.push_back(
                                        affinity.argument_index);
                                }
                            }
                            assert(transfer_index == set.transfers.size());

                            std::vector<PhysicalLocation> locations;
                            locations.reserve(parameter_locations.size());
                            for(std::optional<PhysicalLocation> location:
                                parameter_locations)
                            {
                                assert(location.has_value());
                                locations.push_back(*location);
                            }
                            result.edge_transfers.push_back(
                                {const_cast<BlockEdge *>(edge),
                                 std::move(planned),
                                 std::move(argument_indices),
                                 std::move(locations)});
                            inserted = true;
                            break;
                        }
                }
                if(!inserted)
                {
                    fatal("duplicate JIT materialization transfer point");
                }
            }

            for(size_t index = 0; index < problem.occurrences().size(); ++index)
            {
                const Occurrence &occurrence = problem.occurrences()[index];
                BundleId bundle = bundle_by_occurrence[index];
                PhysicalLocation location =
                    allocation.locations().physical_location_for(bundle);
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
            for(const PlannedEdgeTransferSet &edge: plan.edge_transfers)
            {
                requests.push_back(
                    {edge.edge, edge_split_placement(graph, *edge.edge)});
            }
            std::vector<Block *> blocks = rewriter.stage_edge_splits(requests);
            assert(blocks.size() == plan.edge_transfers.size());

            for(size_t index = 0; index < blocks.size(); ++index)
            {
                Block *block = blocks[index];
                PlannedEdgeTransferSet &edge = plan.edge_transfers[index];
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

                edge.transfers.sources.reserve(edge.argument_indices.size());
                for(uint32_t argument_index: edge.argument_indices)
                {
                    edge.transfers.sources.push_back(
                        block->parameter_at(argument_index).id());
                }
                bool inserted =
                    plan.block_entries.emplace(block, std::move(edge.transfers))
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
                  locations_(std::move(plan.existing_locations)),
                  current_values_(std::move(plan.initial_value_by_bundle))
            {
            }

            RewriteInsertion at_block_entry(RewriteContext &context,
                                            const GraphQueries &,
                                            const Block &block)
            {
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
                auto fixed_operand_copy =
                    fixed_operand_copies_.find(instruction.id());
                pending_fixed_operand_copies_ =
                    fixed_operand_copy == fixed_operand_copies_.end()
                        ? nullptr
                        : &fixed_operand_copy->second;
                auto found = before_instructions_.find(instruction.id());
                return found == before_instructions_.end()
                           ? RewriteInsertion::none()
                           : emit_transfers(context, found->second);
            }

            LocationAssignments
            finish(const NormalizationRemapping &normalization) &&
            {
                assert(pending_fixed_operand_copies_ == nullptr);
                return std::move(locations_).finalize(normalization);
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
                const BundleTransferSet &set = *planned.original;
                bool uses_explicit_sources = !planned.sources.empty();
                std::span<const InstructionId> sources = planned.sources;
                std::vector<InstructionId> current_sources;
                if(!uses_explicit_sources)
                {
                    current_sources.reserve(set.transfers.size());
                    for(const BundleTransfer &transfer: set.transfers)
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
                assert(sources.size() == set.transfers.size());
                EmittedParallelAssignment emitted =
                    emit_parallel_assignment(context, planned.ordered, sources);
                RewriteInsertion::TransferOutputs outputs;
                if(!uses_explicit_sources)
                {
                    for(size_t index = 0; index < set.transfers.size(); ++index)
                    {
                        current_values_[set.transfers[index]
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

            RewriteResult
            emit_fixed_operand_copies(RewriteContext &context,
                                      const Instruction &instruction,
                                      const PlannedFixedOperandCopySet &planned)
            {
                absl::flat_hash_map<uint32_t, size_t> use_by_operand;
                use_by_operand.reserve(planned.operand_indices.size());
                for(size_t index = 0; index < planned.operand_indices.size();
                    ++index)
                {
                    bool inserted =
                        use_by_operand
                            .emplace(planned.operand_indices[index], index)
                            .second;
                    if(!inserted)
                    {
                        fatal("JIT instruction has two fixed operand copies "
                              "for one "
                              "operand");
                    }
                }
                std::vector<InstructionId> sources(
                    planned.operand_indices.size(), InstructionId(0));
                std::vector<bool> found_source(planned.operand_indices.size(),
                                               false);
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
                    fatal(
                        "JIT fixed operand copy names no materialized operand");
                }

                EmittedParallelAssignment emitted =
                    emit_parallel_assignment(context, planned.ordered, sources);
                if(emitted.instructions.empty())
                {
                    return RewriteResult::keep();
                }

                absl::flat_hash_map<uint32_t, InstructionId> replacements;
                for(const auto &[operand_index, assignment]: use_by_operand)
                {
                    replacements.emplace(
                        operand_index,
                        emitted.result_by_assignment[assignment]);
                }
                OperandReplacementResolver resolver(replacements);
                Instruction original = instruction;
                Instruction replacement = rebuild_instruction_with_references(
                    original, *instruction.storage(), resolver, context,
                    InstructionRebuildMode::AlwaysClone);
                emitted.instructions.push_back(replacement);
                switch(replacement.result_class())
                {
                    case ResultClass::None:
                        return RewriteResult::replace_without_result(
                            std::move(emitted.instructions));
                    case ResultClass::ProgramValue:
                        return RewriteResult::replace(
                            std::move(emitted.instructions),
                            ProgramValueRef(replacement));
                    case ResultClass::Snapshot:
                        return RewriteResult::replace(
                            std::move(emitted.instructions),
                            SnapshotRef(replacement));
                    case ResultClass::Count:
                        break;
                }
                fatal("invalid fixed-operand-copy instruction result class");
            }

            absl::flat_hash_map<const Block *, PlannedTransferSet>
                block_entries_;
            absl::flat_hash_map<InstructionId, PlannedTransferSet>
                before_instructions_;
            absl::flat_hash_map<InstructionId, PlannedFixedOperandCopySet>
                fixed_operand_copies_;
            const PlannedFixedOperandCopySet *pending_fixed_operand_copies_ =
                nullptr;
            LocationAssignmentsBuilder locations_;
            std::vector<std::optional<InstructionId>> current_values_;
        };
    }  // namespace

    Result<LocationAssignments, RegisterAllocationError>
    materialize_allocation(CompilationSession &session, ControlFlowGraph &graph,
                           const PreparedAllocationProblem &problem,
                           const AllocationConstraints &constraints,
                           const RegisterAllocationResult &allocation)
    {
        auto plan_result = plan_materialization(problem, constraints,
                                                allocation, *graph.storage());
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
        return Result<LocationAssignments, RegisterAllocationError>::ok(
            std::move(materializer).finish(summary.normalization_remapping));
    }

}  // namespace cl::jit
