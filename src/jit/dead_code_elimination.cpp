#include "jit/dead_code_elimination.h"

#include "jit/graph_rewriter.h"

#include <absl/container/flat_hash_set.h>

namespace cl::jit
{
    Result<bool, JitCompilationError>
    eliminate_dead_code(CompilationSession &session, ControlFlowGraph &graph)
    {
        absl::flat_hash_set<const Instruction *> used;
        absl::flat_hash_set<const Instruction *> dead;

        for(const Block *block: graph.blocks())
        {
            for(const BlockEdge *edge: block->block_successor_edges())
            {
                for(ProgramValueRef argument: edge->arguments())
                {
                    used.insert(argument.instruction());
                }
            }

            for(auto position = block->instructions().rbegin();
                position != block->instructions().rend(); ++position)
            {
                const Instruction *instruction = *position;
                const InstructionKindMetadata &metadata =
                    instruction_kind_metadata(instruction->kind());
                bool can_eliminate =
                    instruction->result_class() != ResultClass::None &&
                    (metadata.may_effects == EffectProfile::None ||
                     metadata.may_effects == EffectProfile::Deoptimize);
                if(can_eliminate && !used.contains(instruction))
                {
                    dead.insert(instruction);
                    continue;
                }

                visit_operand_references(
                    *instruction,
                    [&](uint32_t, OperandClass, ValueRepresentation,
                        Instruction *definition) { used.insert(definition); });
            }
        }

        if(dead.empty())
        {
            return Result<bool, JitCompilationError>::ok(false);
        }

        GraphRewriter rewriter(session, graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [&](RewriteContext &, const GraphQueries &, const Block &,
                const Instruction &instruction) {
                return dead.contains(&instruction) ? RewriteResult::erase()
                                                   : RewriteResult::keep();
            });
        return Result<bool, JitCompilationError>::ok(
            summary.instructions_changed);
    }

}  // namespace cl::jit
