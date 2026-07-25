#include "jit/allocation_materializer.h"

#include "jit/graph_rewriter.h"
#include "jit/parallel_transfer_resolver.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct PlannedTransferSet
        {
            const BundleTransferSet *original;
            ResolvedTransferPlan resolved;
        };

        struct MaterializationPlan
        {
            absl::flat_hash_map<const Block *, PlannedTransferSet>
                block_entries;
            absl::flat_hash_map<const Instruction *, PlannedTransferSet>
                before_instructions;
            LocationAssignmentsBuilder existing_locations;
            std::vector<Instruction *> initial_value_by_bundle;
        };

        Result<MaterializationPlan, RegisterAllocationError>
        plan_materialization(const PreparedAllocationProblem &problem,
                             const AllocationConstraints &constraints,
                             const RegisterAllocationResult &allocation)
        {
            MaterializationPlan result;
            ScratchRegisters scratch_registers;
            for(const RegisterClassDefinition &definition:
                constraints.register_classes())
            {
                scratch_registers[static_cast<size_t>(
                    definition.register_class())] =
                    definition.scratch_register();
            }
            for(const BundleTransferSet &set: allocation.transfers().sets())
            {
                std::vector<ParallelTransfer> transfers;
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
                auto resolved =
                    resolve_parallel_transfers(transfers, scratch_registers);
                if(!resolved)
                {
                    return propagate_failure(std::move(resolved));
                }
                PlannedTransferSet planned{&set, std::move(resolved).value()};

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
                                       .emplace(set.point.instruction(),
                                                std::move(planned))
                                       .second;
                        break;
                    case TransferPoint::Kind::BlockExit:
                    case TransferPoint::Kind::BlockEdge:
                        return Result<MaterializationPlan,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      UnsupportedTransferPoint);
                }
                if(!inserted)
                {
                    fatal("duplicate JIT materialization transfer point");
                }
            }

            std::vector<std::vector<std::pair<LivenessRange, BundleId>>>
                fragments_by_live_range(problem.live_ranges().size());
            for(size_t index = 0; index < allocation.bundles().size(); ++index)
            {
                for(const BundleFragment &fragment:
                    allocation.bundles()[index].fragments)
                {
                    fragments_by_live_range[fragment.source.value()].push_back(
                        {fragment.range, BundleId(index)});
                }
            }
            for(auto &fragments: fragments_by_live_range)
            {
                std::ranges::sort(fragments, {}, [](const auto &fragment) {
                    return fragment.first.start;
                });
            }

            for(size_t index = 0; index < problem.occurrences().size(); ++index)
            {
                const Occurrence &occurrence = problem.occurrences()[index];
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
                BundleId bundle = found->second;
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
                            occurrence.anchor.instruction(),
                            occurrence.anchor.index(), location);
                        break;
                    case OccurrenceAnchor::Kind::InstructionOperand:
                    case OccurrenceAnchor::Kind::BlockEdgeArgument:
                        break;
                }
            }

            result.initial_value_by_bundle.resize(allocation.bundles().size(),
                                                  nullptr);
            for(size_t index = 0; index < allocation.bundles().size(); ++index)
            {
                Instruction *value = nullptr;
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
                    Instruction *candidate =
                        source.origin.program_value().instruction();
                    if(value != nullptr && value != candidate)
                    {
                        fatal("JIT materialization of merged bundle values is "
                              "not implemented");
                    }
                    value = candidate;
                }
                result.initial_value_by_bundle[index] = value;
            }

            return Result<MaterializationPlan, RegisterAllocationError>::ok(
                std::move(result));
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
                auto found = before_instructions_.find(&instruction);
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
                std::vector<Instruction *> sources;
                sources.reserve(set.transfers.size());
                for(const BundleTransfer &transfer: set.transfers)
                {
                    Instruction *source =
                        current_values_[transfer.source.value()];
                    if(source == nullptr)
                    {
                        fatal("JIT bundle transfer has no program value");
                    }
                    sources.push_back(source);
                }

                for(size_t index: planned.resolved.aliasing_transfers)
                {
                    const BundleTransfer &transfer = set.transfers[index];
                    current_values_[transfer.destination.value()] =
                        sources[index];
                }

                if(planned.resolved.steps.empty())
                {
                    return RewriteInsertion::none();
                }

                RewriteInsertion::InstructionSequence instructions;
                RewriteInsertion::TransferOutputs outputs;
                std::vector<Instruction *> step_values;
                step_values.reserve(planned.resolved.steps.size());
                for(const ResolvedTransferStep &step: planned.resolved.steps)
                {
                    Instruction *source = nullptr;
                    switch(step.source.kind())
                    {
                        case ResolvedTransferSource::Kind::OriginalTransfer:
                            source = sources[step.source.index()];
                            break;
                        case ResolvedTransferSource::Kind::Step:
                            source = step_values[step.source.index()];
                            break;
                    }

                    Instruction *move = nullptr;
                    switch(source->value_representation())
                    {
                        case ValueRepresentation::TaggedValue:
                            move = context.make_instruction<MovInstruction>(
                                TaggedValueRef(source));
                            break;
                        case ValueRepresentation::F64:
                            move = context.make_instruction<MovF64Instruction>(
                                F64Ref(source));
                            break;
                        case ValueRepresentation::None:
                        case ValueRepresentation::Count:
                            fatal("invalid JIT bundle transfer "
                                  "representation");
                    }
                    instructions.push_back(move);
                    step_values.push_back(move);
                    locations_.assign(ProgramValueRef(move), step.destination);

                    if(step.original_parallel_transfer_index >= 0)
                    {
                        int transfer_index =
                            step.original_parallel_transfer_index;
                        const BundleTransfer &transfer =
                            set.transfers[transfer_index];
                        current_values_[transfer.destination.value()] = move;
                        outputs.emplace_back(
                            ProgramValueRef(sources[transfer_index]),
                            ProgramValueRef(move));
                    }
                }

                return RewriteInsertion::insert_transfers(
                    std::move(instructions), std::move(outputs));
            }

            absl::flat_hash_map<const Block *, PlannedTransferSet>
                block_entries_;
            absl::flat_hash_map<const Instruction *, PlannedTransferSet>
                before_instructions_;
            LocationAssignmentsBuilder locations_;
            std::vector<Instruction *> current_values_;
        };
    }  // namespace

    Result<LocationAssignments, RegisterAllocationError>
    materialize_allocation(CompilationSession &session, ControlFlowGraph &graph,
                           const PreparedAllocationProblem &problem,
                           const AllocationConstraints &constraints,
                           const RegisterAllocationResult &allocation)
    {
        auto plan_result =
            plan_materialization(problem, constraints, allocation);
        if(!plan_result)
        {
            return propagate_failure(std::move(plan_result));
        }

        AllocationMaterializer materializer(std::move(plan_result).value());
        GraphRewriter rewriter(session, graph);
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), materializer);
        return Result<LocationAssignments, RegisterAllocationError>::ok(
            std::move(materializer).finish(summary.normalization_remapping));
    }

}  // namespace cl::jit
