#include "jit/side_exit_lowering.h"

#include "jit/compilation_session.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_rewriter.h"
#include "jit/instruction_reconstruction.h"
#include "jit/use_lists.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct InstructionProgramPosition
        {
            const Block *block;
            uint32_t ordinal;
        };

        struct PlannedSideExit
        {
            InstructionId owner;
            InstructionId snapshot;
            std::vector<InstructionId> retained;
            std::vector<ProgramValueRef> arguments;
        };

        struct BuiltSideExitRegion
        {
            SideExitRegion *region;
            std::vector<ProgramValueRef> arguments;
        };

        using InstructionPositions =
            absl::flat_hash_map<InstructionId, InstructionProgramPosition>;

        BinaryArithmeticSMIWithSideExitSubkind machine_arithmetic_subkind(
            BinaryArithmeticSMIWithSnapshotSubkind subkind)
        {
            switch(subkind)
            {
                case BinaryArithmeticSMIWithSnapshotSubkind::AddSMI:
                    return BinaryArithmeticSMIWithSideExitSubkind::
                        AddSMIWithSideExit;
                case BinaryArithmeticSMIWithSnapshotSubkind::SubSMI:
                    return BinaryArithmeticSMIWithSideExitSubkind::
                        SubSMIWithSideExit;
                case BinaryArithmeticSMIWithSnapshotSubkind::MulSMI:
                    return BinaryArithmeticSMIWithSideExitSubkind::
                        MulSMIWithSideExit;
            }
            __builtin_unreachable();
        }

        PlannedSideExit
        plan_side_exit(const ControlFlowGraph &graph, const Block &block,
                       const Instruction &owner, SnapshotRef snapshot,
                       const SunkInstructionIds &sunk_instructions,
                       const InstructionPositions &positions,
                       uint32_t owner_ordinal)
        {
            InstructionId snapshot_id = snapshot.instruction_id();
            if(!sunk_instructions.contains(snapshot_id))
            {
                fatal("JIT side-exit Snapshot was not selected for sinking");
            }

            absl::flat_hash_set<InstructionId> retained_set;
            auto collect = [&](auto &self, InstructionId id) -> void {
                if(!retained_set.insert(id).second)
                {
                    return;
                }

                auto position = positions.find(id);
                if(position == positions.end() ||
                   position->second.block != &block ||
                   position->second.ordinal >= owner_ordinal)
                {
                    fatal("JIT sunk instruction is not defined before its side "
                          "exit in the same block");
                }

                Instruction instruction = graph.storage()->instruction(id);
                visit_operand_references(
                    instruction,
                    [&](uint32_t, OperandClass, ValueRepresentationRequirement,
                        InstructionId definition) {
                        if(sunk_instructions.contains(definition))
                        {
                            self(self, definition);
                        }
                    });
            };
            collect(collect, snapshot_id);

            std::vector<InstructionId> retained(retained_set.begin(),
                                                retained_set.end());
            std::ranges::sort(retained, [&](InstructionId lhs,
                                            InstructionId rhs) {
                return positions.at(lhs).ordinal < positions.at(rhs).ordinal;
            });

            absl::flat_hash_set<InstructionId> available;
            absl::flat_hash_set<InstructionId> argument_set;
            std::vector<ProgramValueRef> arguments;
            for(InstructionId id: retained)
            {
                Instruction instruction = graph.storage()->instruction(id);
                visit_operand_references(
                    instruction, [&](uint32_t, OperandClass operand_class,
                                     ValueRepresentationRequirement,
                                     InstructionId definition) {
                        if(available.contains(definition))
                        {
                            return;
                        }
                        if(retained_set.contains(definition))
                        {
                            fatal("JIT side-exit instruction precedes a sunk "
                                  "dependency");
                        }
                        if(operand_class != OperandClass::ProgramValue)
                        {
                            fatal("JIT side exit has a non-program-value "
                                  "argument");
                        }
                        if(argument_set.insert(definition).second)
                        {
                            arguments.emplace_back(
                                graph.storage()->instruction(definition));
                        }
                    });
                available.insert(id);
            }

            return PlannedSideExit{owner.id(), snapshot_id, std::move(retained),
                                   std::move(arguments)};
        }

        std::vector<PlannedSideExit>
        plan_side_exit_lowering(const ControlFlowGraph &graph,
                                const SunkInstructionIds &sunk_instructions)
        {
            if(graph.ir_level() != IRLevel::Core)
            {
                fatal("side-exit lowering requires a Core IR graph");
            }

            InstructionPositions positions;
            std::vector<PlannedSideExit> plans;
            uint32_t ordinal = 0;
            for(const Block *block: graph.blocks())
            {
                for(Instruction instruction: block->instructions())
                {
                    if(sunk_instructions.contains(instruction.id()))
                    {
                        positions.emplace(
                            instruction.id(),
                            InstructionProgramPosition{block, ordinal});
                    }
                    if(instruction_family_kind(instruction.kind()) ==
                       InstructionFamilyKind::BinaryArithmeticSMIWithSnapshot)
                    {
                        plans.push_back(plan_side_exit(
                            graph, *block, instruction,
                            instruction
                                .as<BinaryArithmeticSMIWithSnapshotInstruction>()
                                .snapshot(),
                            sunk_instructions, positions, ordinal));
                    }
                    switch(instruction.kind())
                    {
                        case InstructionKind::InlineTagGuard:
                            plans.push_back(plan_side_exit(
                                graph, *block, instruction,
                                instruction.as<InlineTagGuardInstruction>()
                                    .snapshot(),
                                sunk_instructions, positions, ordinal));
                            break;
                        case InstructionKind::ResumeInInterpreter:
                            plans.push_back(plan_side_exit(
                                graph, *block, instruction,
                                instruction.as<ResumeInInterpreterInstruction>()
                                    .snapshot(),
                                sunk_instructions, positions, ordinal));
                            break;
                        default:
                            break;
                    }
                    ++ordinal;
                }
            }

            absl::flat_hash_set<InstructionId> retained;
            absl::flat_hash_map<InstructionId,
                                absl::flat_hash_set<InstructionId>>
                owners_for_snapshot;
            for(const PlannedSideExit &plan: plans)
            {
                for(InstructionId id: plan.retained)
                {
                    retained.insert(id);
                }
                owners_for_snapshot[plan.snapshot].insert(plan.owner);
            }
            if(retained != sunk_instructions)
            {
                fatal("a sunk JIT instruction is not retained by a side exit");
            }

            GraphQueries queries = graph.prepare_queries(GraphQuery::Uses);
            for(InstructionId id: retained)
            {
                Instruction instruction = graph.storage()->instruction(id);
                const Uses &uses = queries.uses_of(instruction);
                if(uses.n_block_argument_uses() != 0)
                {
                    fatal("a sunk JIT instruction is used by a block edge");
                }
                for(const InstructionUse &use: uses.instruction_uses())
                {
                    if(retained.contains(use.instruction))
                    {
                        continue;
                    }
                    auto owners = owners_for_snapshot.find(id);
                    if(owners == owners_for_snapshot.end() ||
                       !owners->second.contains(use.instruction))
                    {
                        fatal("a sunk JIT instruction has an executable use");
                    }
                }
            }
            return plans;
        }

        class SideExitLoweringRewrite
        {
        public:
            SideExitLoweringRewrite(std::span<const PlannedSideExit> plans,
                                    const SunkInstructionIds &sunk_instructions)
                : plans_(plans), sunk_instructions_(&sunk_instructions)
            {
                for(size_t index = 0; index < plans_.size(); ++index)
                {
                    plan_by_owner_.emplace(plans_[index].owner, index);
                }
            }

            RewriteResult rewrite_instruction(RewriteContext &context,
                                              const GraphQueries &,
                                              const Block &,
                                              const Instruction &instruction)
            {
                if(sunk_instructions_->contains(instruction.id()))
                {
                    return RewriteResult::erase();
                }

                auto found = plan_by_owner_.find(instruction.id());
                if(found == plan_by_owner_.end())
                {
                    return RewriteResult::keep();
                }

                const PlannedSideExit &plan = plans_[found->second];
                if(instruction_family_kind(instruction.kind()) ==
                   InstructionFamilyKind::BinaryArithmeticSMIWithSnapshot)
                {
                    const BuiltSideExitRegion &region =
                        region_for_snapshot(context, plan);
                    auto arithmetic =
                        instruction
                            .as<BinaryArithmeticSMIWithSnapshotInstruction>();
                    return RewriteResult::replace(
                        context.make_instruction<
                            BinaryArithmeticSMIWithSideExitInstruction>(
                            machine_arithmetic_subkind(arithmetic.subkind()),
                            arithmetic.lhs(), arithmetic.rhs(),
                            region.arguments, region.region->id()));
                }
                switch(instruction.kind())
                {
                    case InstructionKind::InlineTagGuard:
                        {
                            const BuiltSideExitRegion &region =
                                region_for_snapshot(context, plan);
                            InlineTagGuardInstruction guard =
                                instruction.as<InlineTagGuardInstruction>();
                            return RewriteResult::replace(
                                context.make_instruction<
                                    InlineTagGuardWithSideExitInstruction>(
                                    guard.value(), region.arguments,
                                    guard.expected_class(),
                                    region.region->id()));
                        }
                    case InstructionKind::ResumeInInterpreter:
                        {
                            const BuiltSideExitRegion &region =
                                region_for_snapshot(context, plan);
                            return RewriteResult::replace(
                                context.make_instruction<
                                    ResumeInInterpreterWithSideExitInstruction>(
                                    region.arguments, region.region->id()));
                        }
                    default:
                        fatal("unsupported planned JIT side-exit owner");
                }
            }

        private:
            Instruction make_region_parameter(RewriteContext &context,
                                              ProgramValueRef argument)
            {
                Instruction value =
                    context.instruction(argument.instruction_id());
                switch(value.value_representation())
                {
                    case ValueRepresentation::TaggedValue:
                        return context.make_instruction<ParameterInstruction>();
                    case ValueRepresentation::F64:
                        return context
                            .make_instruction<ParameterF64Instruction>();
                    case ValueRepresentation::Pointer:
                        return context
                            .make_instruction<ParameterPointerInstruction>();
                    case ValueRepresentation::None:
                    case ValueRepresentation::Count:
                        fatal("side-exit region argument has no value "
                              "representation");
                }
                fatal("invalid side-exit region argument representation");
            }

            class RegionReferenceResolver
            {
            public:
                RegionReferenceResolver(
                    RewriteContext &context,
                    const absl::flat_hash_map<InstructionId, InstructionId>
                        &remapping)
                    : context_(&context), remapping_(&remapping)
                {
                }

                InstructionId resolve(InstructionId def) const
                {
                    auto found = remapping_->find(def);
                    if(found == remapping_->end())
                    {
                        fatal("side-exit region clone has an unresolved "
                              "operand");
                    }
                    return found->second;
                }

                ProgramValueRef resolve(ProgramValueRef def) const
                {
                    return ProgramValueRef(
                        context_->instruction(resolve(def.instruction_id())));
                }

                TaggedValueRef resolve(TaggedValueRef def) const
                {
                    return TaggedValueRef(
                        context_->instruction(resolve(def.instruction_id())));
                }

                F64Ref resolve(F64Ref def) const
                {
                    return F64Ref(
                        context_->instruction(resolve(def.instruction_id())));
                }

                PointerRef resolve(PointerRef def) const
                {
                    return PointerRef(
                        context_->instruction(resolve(def.instruction_id())));
                }

                SnapshotRef resolve(SnapshotRef def) const
                {
                    return SnapshotRef(
                        context_->instruction(resolve(def.instruction_id())));
                }

                BlockEdge *resolve(BlockEdge *) const
                {
                    fatal("side-exit region clone cannot contain block edges");
                }

                template <typename T> T resolve_attribute(T attribute) const
                {
                    return attribute;
                }

                std::vector<ProgramValueRef>
                resolve(ProgramValueRefRange defs) const
                {
                    std::vector<ProgramValueRef> result;
                    result.reserve(defs.size());
                    for(size_t index = 0; index < defs.size(); ++index)
                    {
                        result.push_back(resolve(defs[index]));
                    }
                    return result;
                }

                template <ValueRepresentation Representation>
                auto
                resolve(RepresentedValueRefRange<Representation> defs) const
                {
                    using Reference = decltype(defs[size_t{0}]);
                    std::vector<Reference> result;
                    result.reserve(defs.size());
                    for(size_t index = 0; index < defs.size(); ++index)
                    {
                        result.push_back(resolve(defs[index]));
                    }
                    return result;
                }

            private:
                RewriteContext *context_;
                const absl::flat_hash_map<InstructionId, InstructionId>
                    *remapping_;
            };

            const BuiltSideExitRegion &
            region_for_snapshot(RewriteContext &context,
                                const PlannedSideExit &plan)
            {
                auto found = region_by_snapshot_.find(plan.snapshot);
                if(found != region_by_snapshot_.end())
                {
                    return found->second;
                }

                absl::flat_hash_map<InstructionId, InstructionId> remapping;
                std::vector<InstructionId> parameter_ids;
                std::vector<InstructionId> instruction_ids;
                std::vector<ProgramValueRef> arguments;
                parameter_ids.reserve(plan.arguments.size());
                instruction_ids.reserve(plan.retained.size());
                arguments.reserve(plan.arguments.size());

                for(ProgramValueRef argument: plan.arguments)
                {
                    Instruction parameter =
                        make_region_parameter(context, argument);
                    parameter_ids.push_back(parameter.id());
                    arguments.push_back(argument);
                    remapping.emplace(argument.instruction_id(),
                                      parameter.id());
                }

                for(InstructionId id: plan.retained)
                {
                    Instruction instruction = context.instruction(id);
                    RegionReferenceResolver resolver(context, remapping);
                    Instruction clone =
                        instruction.kind() == InstructionKind::Snapshot
                            ? Instruction(context.make_instruction<
                                          ExitToInterpreterInstruction>(
                                  resolver.resolve(
                                      instruction.as<SnapshotInstruction>()
                                          .captured_values()),
                                  instruction.as<SnapshotInstruction>()
                                      .resume_pc_offset()))
                            : rebuild_instruction_with_references(
                                  instruction, *instruction.storage(), resolver,
                                  context, InstructionRebuildMode::AlwaysClone);
                    instruction_ids.push_back(clone.id());
                    remapping.emplace(id, clone.id());
                }

                SideExitRegion *region = context.make_side_exit_region(
                    parameter_ids, instruction_ids);
                auto [position, inserted] = region_by_snapshot_.emplace(
                    plan.snapshot,
                    BuiltSideExitRegion{region, std::move(arguments)});
                assert(inserted);
                return position->second;
            }

            std::span<const PlannedSideExit> plans_;
            const SunkInstructionIds *sunk_instructions_;
            absl::flat_hash_map<InstructionId, size_t> plan_by_owner_;
            absl::flat_hash_map<InstructionId, BuiltSideExitRegion>
                region_by_snapshot_;
        };

        std::optional<SnapshotRef>
        side_exit_snapshot_for(Instruction instruction)
        {
            if(instruction_family_kind(instruction.kind()) ==
               InstructionFamilyKind::BinaryArithmeticSMIWithSnapshot)
            {
                return instruction
                    .as<BinaryArithmeticSMIWithSnapshotInstruction>()
                    .snapshot();
            }
            switch(instruction.kind())
            {
                case InstructionKind::InlineTagGuard:
                    return instruction.as<InlineTagGuardInstruction>()
                        .snapshot();
                case InstructionKind::ResumeInInterpreter:
                    return instruction.as<ResumeInInterpreterInstruction>()
                        .snapshot();
                default:
                    return std::nullopt;
            }
        }
    }  // namespace

    SunkInstructionIds sink_snapshots(const ControlFlowGraph &graph)
    {
        SunkInstructionIds result;
        for(const Block *block: graph.blocks())
        {
            for(Instruction instruction: block->instructions())
            {
                std::optional<SnapshotRef> snapshot =
                    side_exit_snapshot_for(instruction);
                if(snapshot)
                {
                    result.insert(snapshot->instruction_id());
                }
            }
        }
        return result;
    }

    Result<bool, JitCompilationError>
    lower_side_exits(CompilationSession &session, ControlFlowGraph &graph,
                     const SunkInstructionIds &sunk_instructions)
    {
        std::vector<PlannedSideExit> plans =
            plan_side_exit_lowering(graph, sunk_instructions);
        SideExitLoweringRewrite rewrite(plans, sunk_instructions);
        GraphRewriter rewriter(session, graph);
        rewriter.set_target_ir_level(IRLevel::Machine);
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), rewrite);
        return Result<bool, JitCompilationError>::ok(
            summary.instructions_changed || summary.ir_level_changed);
    }

}  // namespace cl::jit
