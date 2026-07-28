#include "jit/side_exit_lowering.h"

#include "jit/compilation_session.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_rewriter.h"
#include "jit/use_lists.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
            std::vector<InstructionId> retained;
            std::vector<ProgramValueRef> inputs;
        };

        using InstructionPositions =
            absl::flat_hash_map<InstructionId, InstructionProgramPosition>;

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
            absl::flat_hash_set<InstructionId> input_set;
            std::vector<ProgramValueRef> inputs;
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
                            fatal(
                                "JIT side exit has a non-program-value input");
                        }
                        if(input_set.insert(definition).second)
                        {
                            inputs.emplace_back(
                                graph.storage()->instruction(definition));
                        }
                    });
                available.insert(id);
            }

            return PlannedSideExit{owner.id(), std::move(retained),
                                   std::move(inputs)};
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
                    if(instruction.kind() ==
                       InstructionKind::ResumeInInterpreter)
                    {
                        plans.push_back(plan_side_exit(
                            graph, *block, instruction,
                            instruction.as<ResumeInInterpreterInstruction>()
                                .snapshot(),
                            sunk_instructions, positions, ordinal));
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
                owners_for_snapshot[plan.retained.back()].insert(plan.owner);
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
                    return RewriteResult::detach();
                }

                auto found = plan_by_owner_.find(instruction.id());
                if(found == plan_by_owner_.end())
                {
                    return RewriteResult::keep();
                }

                const PlannedSideExit &plan = plans_[found->second];
                SideExitId side_exit =
                    context.emplace_side_exit(plan.inputs, plan.retained);
                return RewriteResult::replace(
                    context.make_instruction<
                        ResumeInInterpreterWithSideExitInstruction>(plan.inputs,
                                                                    side_exit));
            }

        private:
            std::span<const PlannedSideExit> plans_;
            const SunkInstructionIds *sunk_instructions_;
            absl::flat_hash_map<InstructionId, size_t> plan_by_owner_;
        };
    }  // namespace

    SunkInstructionIds sink_snapshots(const ControlFlowGraph &graph)
    {
        SunkInstructionIds result;
        for(const Block *block: graph.blocks())
        {
            for(Instruction instruction: block->instructions())
            {
                if(instruction.kind() == InstructionKind::Snapshot)
                {
                    result.insert(instruction.id());
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
