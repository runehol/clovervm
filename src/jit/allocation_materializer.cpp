#include "jit/allocation_materializer.h"

#include "jit/graph_rewriter.h"
#include "jit/parallel_assignment_resolver.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
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

        struct MaterializationPlan
        {
            absl::flat_hash_map<const Block *, PlannedTransferSet>
                block_entries;
            absl::flat_hash_map<InstructionId, PlannedTransferSet>
                before_instructions;
            std::vector<PlannedEdgeTransferSet> edge_transfers;
            LocationAssignmentsBuilder existing_locations;
            std::vector<std::optional<InstructionId>> initial_value_by_bundle;
        };

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
                                std::vector<const EdgeAffinity *>>
                affinities_by_edge;
            for(const EdgeAffinity &affinity: problem.edge_affinities())
            {
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
                        {allocation.locations().location_for(transfer.source),
                         allocation.locations().location_for(
                             transfer.destination),
                         register_class});
                }
                auto ordered =
                    plan_physical_assignments(transfers, scratch_registers);
                if(!ordered)
                {
                    return propagate_failure(std::move(ordered));
                }
                PlannedTransferSet planned{
                    &set, std::move(ordered).value(), {}};

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
                            for(const EdgeAffinity *affinity_pointer:
                                affinities_by_edge.at(edge))
                            {
                                const EdgeAffinity &affinity =
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
                                location =
                                    allocation.locations().location_for(source);
                                if(source != destination &&
                                   !location->aliases(
                                       allocation.locations().location_for(
                                           destination)))
                                {
                                    assert(transfer_index <
                                           set.transfers.size());
                                    const BundleTransfer &transfer =
                                        set.transfers[transfer_index++];
                                    assert(transfer.source == source);
                                    assert(transfer.destination == destination);
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
                    allocation.locations().location_for(bundle);
                switch(occurrence.anchor.kind())
                {
                    case OccurrenceAnchor::Kind::InstructionResult:
                        {
                            const LiveRange &range =
                                problem.live_ranges()[occurrence.live_range
                                                          .value()];
                            result.existing_locations.assign(
                                range.origin.program_value(), location);
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
            }
            plan.edge_transfers.clear();
        }

        class AllocationMaterializer
        {
        public:
            explicit AllocationMaterializer(MaterializationPlan plan)
                : block_entries_(std::move(plan.block_entries)),
                  before_instructions_(std::move(plan.before_instructions)),
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

            RewriteInsertion before_instruction(RewriteContext &context,
                                                const GraphQueries &,
                                                const Block &,
                                                const Instruction &instruction)
            {
                auto found = before_instructions_.find(instruction.id());
                return found == before_instructions_.end()
                           ? RewriteInsertion::none()
                           : emit_transfers(context, found->second);
            }

            LocationAssignments
            finish(const NormalizationRemapping &normalization) &&
            {
                return std::move(locations_).finalize(normalization);
            }

        private:
            RewriteInsertion emit_transfers(RewriteContext &context,
                                            const PlannedTransferSet &planned)
            {
                const BundleTransferSet &set = *planned.original;
                bool uses_explicit_sources = !planned.sources.empty();
                std::vector<InstructionId> sources = planned.sources;
                if(!uses_explicit_sources)
                {
                    sources.reserve(set.transfers.size());
                    for(const BundleTransfer &transfer: set.transfers)
                    {
                        std::optional<InstructionId> source =
                            current_values_[transfer.source.value()];
                        if(!source.has_value())
                        {
                            fatal("JIT bundle transfer has no program value");
                        }
                        sources.push_back(*source);
                    }
                }
                assert(sources.size() == set.transfers.size());

                for(size_t index: planned.ordered.aliasing_assignments)
                {
                    if(!uses_explicit_sources)
                    {
                        const BundleTransfer &transfer = set.transfers[index];
                        current_values_[transfer.destination.value()] =
                            sources[index];
                    }
                }

                if(planned.ordered.moves.empty())
                {
                    return RewriteInsertion::none();
                }

                RewriteInsertion::InstructionSequence instructions;
                RewriteInsertion::TransferOutputs outputs;
                std::vector<InstructionId> move_values;
                move_values.reserve(planned.ordered.moves.size());
                for(const OrderedMove<PhysicalLocation> &move:
                    planned.ordered.moves)
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
                            fatal("invalid JIT bundle transfer "
                                  "representation");
                    }
                    if(!output.has_value())
                    {
                        fatal("invalid resolved JIT transfer locations");
                    }
                    instructions.push_back(*output);
                    move_values.push_back(output->id());
                    locations_.assign(ProgramValueRef(*output),
                                      move.destination);

                    if(move.original_assignment_index >= 0)
                    {
                        int transfer_index = move.original_assignment_index;
                        if(!uses_explicit_sources)
                        {
                            const BundleTransfer &transfer =
                                set.transfers[transfer_index];
                            current_values_[transfer.destination.value()] =
                                output->id();
                        }
                        outputs.emplace_back(
                            ProgramValueRef(
                                context.instruction(sources[transfer_index])),
                            ProgramValueRef(*output));
                    }
                }

                return RewriteInsertion::insert_transfers(
                    std::move(instructions), std::move(outputs));
            }

            absl::flat_hash_map<const Block *, PlannedTransferSet>
                block_entries_;
            absl::flat_hash_map<InstructionId, PlannedTransferSet>
                before_instructions_;
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
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), materializer);
        return Result<LocationAssignments, RegisterAllocationError>::ok(
            std::move(materializer).finish(summary.normalization_remapping));
    }

}  // namespace cl::jit
