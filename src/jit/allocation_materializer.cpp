#include "jit/allocation_materializer.h"

#include "jit/graph_rewriter.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct MaterializationPlan
        {
            absl::flat_hash_map<const Block *, const BundleTransferSet *>
                block_entries;
            absl::flat_hash_map<const Instruction *, const BundleTransferSet *>
                before_instructions;
            LocationAssignmentsBuilder existing_locations;
            std::vector<Instruction *> initial_value_by_bundle;
        };

        Result<MaterializationPlan, RegisterAllocationError>
        plan_materialization(const PreparedAllocationProblem &problem,
                             const RegisterAllocationResult &allocation)
        {
            MaterializationPlan result;
            for(const BundleTransferSet &set: allocation.transfers().sets())
            {
                bool has_non_aliasing_transfer = false;
                for(const BundleTransfer &transfer: set.transfers)
                {
                    PhysicalLocation source =
                        allocation.locations().location_for(transfer.source);
                    PhysicalLocation destination =
                        allocation.locations().location_for(
                            transfer.destination);
                    if(source.aliases(destination))
                    {
                        continue;
                    }
                    if(has_non_aliasing_transfer)
                    {
                        return Result<MaterializationPlan,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      RequiresParallelTransferResolution);
                    }
                    if(source.is_stack() && destination.is_stack())
                    {
                        return Result<MaterializationPlan,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      RequiresTransferScratch);
                    }
                    has_non_aliasing_transfer = true;
                }

                bool inserted = false;
                switch(set.point.kind())
                {
                    case TransferPoint::Kind::BlockEntry:
                        inserted = result.block_entries
                                       .emplace(set.point.block(), &set)
                                       .second;
                        break;
                    case TransferPoint::Kind::BeforeInstruction:
                        inserted = result.before_instructions
                                       .emplace(set.point.instruction(), &set)
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
            AllocationMaterializer(const RegisterAllocationResult &allocation,
                                   MaterializationPlan plan)
                : allocation_(&allocation),
                  block_entries_(std::move(plan.block_entries)),
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
                           : emit_transfers(context, *found->second);
            }

            RewriteInsertion before_instruction(RewriteContext &context,
                                                const GraphQueries &,
                                                const Block &,
                                                const Instruction &instruction)
            {
                auto found = before_instructions_.find(&instruction);
                return found == before_instructions_.end()
                           ? RewriteInsertion::none()
                           : emit_transfers(context, *found->second);
            }

            LocationAssignments
            finish(const NormalizationRemapping &normalization) &&
            {
                return std::move(locations_).finalize(normalization);
            }

        private:
            RewriteInsertion emit_transfers(RewriteContext &context,
                                            const BundleTransferSet &set)
            {
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

                const BundleTransfer *transfer_to_emit = nullptr;
                Instruction *source_to_emit = nullptr;
                for(size_t index = 0; index < set.transfers.size(); ++index)
                {
                    const BundleTransfer &transfer = set.transfers[index];
                    PhysicalLocation source =
                        allocation_->locations().location_for(transfer.source);
                    PhysicalLocation destination =
                        allocation_->locations().location_for(
                            transfer.destination);
                    if(!source.aliases(destination))
                    {
                        if(transfer_to_emit != nullptr)
                        {
                            fatal("unresolved parallel JIT transfer reached "
                                  "materialization");
                        }
                        transfer_to_emit = &transfer;
                        source_to_emit = sources[index];
                        continue;
                    }
                    current_values_[transfer.destination.value()] =
                        sources[index];
                }

                if(transfer_to_emit == nullptr)
                {
                    return RewriteInsertion::none();
                }

                Instruction *move = nullptr;
                switch(source_to_emit->value_representation())
                {
                    case ValueRepresentation::TaggedValue:
                        move = context.make_instruction<MovInstruction>(
                            TaggedValueRef(source_to_emit));
                        break;
                    case ValueRepresentation::F64:
                        move = context.make_instruction<MovF64Instruction>(
                            F64Ref(source_to_emit));
                        break;
                    case ValueRepresentation::None:
                    case ValueRepresentation::Count:
                        fatal("invalid JIT bundle transfer representation");
                }

                current_values_[transfer_to_emit->destination.value()] = move;
                locations_.assign(ProgramValueRef(move),
                                  allocation_->locations().location_for(
                                      transfer_to_emit->destination));
                return RewriteInsertion::insert_transfers(
                    {move},
                    {{ProgramValueRef(source_to_emit), ProgramValueRef(move)}});
            }

            const RegisterAllocationResult *allocation_;
            absl::flat_hash_map<const Block *, const BundleTransferSet *>
                block_entries_;
            absl::flat_hash_map<const Instruction *, const BundleTransferSet *>
                before_instructions_;
            LocationAssignmentsBuilder locations_;
            std::vector<Instruction *> current_values_;
        };
    }  // namespace

    Result<LocationAssignments, RegisterAllocationError>
    materialize_allocation(CompilationSession &session, ControlFlowGraph &graph,
                           const PreparedAllocationProblem &problem,
                           const RegisterAllocationResult &allocation)
    {
        auto plan_result = plan_materialization(problem, allocation);
        if(!plan_result)
        {
            return propagate_failure(std::move(plan_result));
        }

        AllocationMaterializer materializer(allocation,
                                            std::move(plan_result).value());
        GraphRewriter rewriter(session, graph);
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), materializer);
        return Result<LocationAssignments, RegisterAllocationError>::ok(
            std::move(materializer).finish(summary.normalization_remapping));
    }

}  // namespace cl::jit
