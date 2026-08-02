#include "jit/tagged_value_guard_elimination.h"

#include "jit/graph_rewriter.h"
#include "jit/tagged_value_facts.h"

namespace cl::jit
{
    Result<bool, JitCompilationError>
    eliminate_redundant_tagged_value_guards(CompilationSession &session,
                                            ControlFlowGraph &graph)
    {
        GraphRewriter rewriter(session, graph);
        RewriteSummary summary = rewriter.rewrite_instructions(
            InstructionTraversal().with_queries(GraphQuery::TaggedValueFacts),
            [](RewriteContext &, const GraphQueries &queries, const Block &,
               const Instruction &instruction) {
                if(instruction.kind() != InstructionKind::InlineTagGuard)
                {
                    return RewriteResult::keep();
                }

                InlineTagGuardInstruction guard =
                    instruction.as<InlineTagGuardInstruction>();
                TaggedValueSet input_tags = queries.tagged_value_facts_of(
                    ProgramValueRef(guard.value()));
                TaggedValueSet accepted_tags =
                    TaggedValueSet::from_class(guard.expected_class());
                if(input_tags.is_subset_of(accepted_tags))
                {
                    return RewriteResult::replace_with_def(
                        ProgramValueRef(guard.value()));
                }
                return RewriteResult::keep();
            });
        return Result<bool, JitCompilationError>::ok(
            summary.instructions_changed);
    }

}  // namespace cl::jit
