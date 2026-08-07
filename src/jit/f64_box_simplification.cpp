#include "jit/f64_box_simplification.h"

#include "jit/graph_rewriter.h"

namespace cl::jit
{
    Result<bool, JitCompilationError>
    simplify_f64_boxing(CompilationSession &session, ControlFlowGraph &graph)
    {
        GraphRewriter rewriter(session, graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal(),
            [](RewriteContext &context, const GraphQueries &, const Block &,
               const Instruction &instruction) {
                if(instruction.kind() != InstructionKind::UnboxF64)
                {
                    return RewriteResult::keep();
                }

                UnboxF64Instruction unbox =
                    instruction.as<UnboxF64Instruction>();
                Instruction source =
                    context.instruction(unbox.source().instruction_id());
                if(source.kind() != InstructionKind::BoxF64)
                {
                    return RewriteResult::keep();
                }

                return RewriteResult::replace_with_def(
                    source.as<BoxF64Instruction>().source());
            });
        return Result<bool, JitCompilationError>::ok(
            summary.instructions_changed);
    }

}  // namespace cl::jit
