#include "jit/f64_box_simplification.h"

#include "jit/graph_rewriter.h"
#include "jit/use_lists.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace cl::jit
{
    namespace
    {
        RewriteSummary simplify_local_f64_boxing(CompilationSession &session,
                                                 ControlFlowGraph &graph)
        {
            GraphRewriter rewriter(session, graph);
            return rewriter.rewrite_instructions(
                InstructionTraversal(),
                [](RewriteContext &context, const GraphQueries &, const Block &,
                   const Instruction &instruction) {
                    switch(CL_JIT_INSTRUCTION_MATCH(instruction))
                    {
                        CL_JIT_INSTRUCTION_CASE(UnboxF64, unbox)
                        {
                            Instruction source = context.instruction(
                                unbox.source().instruction_id());
                            if(source.kind() != InstructionKind::BoxF64)
                            {
                                return RewriteResult::keep();
                            }
                            return RewriteResult::replace_with_def(
                                source.as<BoxF64Instruction>().source());
                        }
                        default:
                            return RewriteResult::keep();
                    }
                });
        }

        bool box_is_identity_discardable_on_edge(
            const GraphQueries &queries, const BoxF64Instruction &box,
            const BlockEdge &edge, InstructionId destination_parameter)
        {
            const Uses &uses = queries.uses_of(box);
            for(const InstructionUse &use: uses.instruction_uses())
            {
                Instruction user =
                    queries.graph().storage()->instruction(use.instruction);
                if(user.kind() != InstructionKind::Snapshot)
                {
                    return false;
                }
            }

            size_t uses_on_edge = 0;
            for(const BlockArgumentUse &use: uses.block_argument_uses())
            {
                if(use.edge != &edge)
                {
                    continue;
                }
                ++uses_on_edge;
                if(use.argument_index >=
                       edge.target()->parameter_ids().size() ||
                   edge.target()->parameter_ids()[use.argument_index] !=
                       destination_parameter)
                {
                    return false;
                }
            }
            return uses_on_edge == 1;
        }

        std::optional<F64Ref> identity_discardable_box_source(
            RewriteContext &context, const GraphQueries &queries,
            IncomingArgument incoming, InstructionId destination_parameter)
        {
            Instruction argument =
                context.instruction(incoming.value.instruction_id());
            switch(CL_JIT_INSTRUCTION_MATCH(argument))
            {
                CL_JIT_INSTRUCTION_CASE(BoxF64, box)
                {
                    const BlockEdge *edge =
                        queries.graph().storage()->block_edge(incoming.edge);
                    if(!box_is_identity_discardable_on_edge(
                           queries, box, *edge, destination_parameter))
                    {
                        return std::nullopt;
                    }
                    return box.source();
                }
                default:
                    return std::nullopt;
            }
        }

        class F64BlockParameterRewrite
        {
        public:
            BlockParameterRewrite
            block_parameter(RewriteContext &context,
                            const GraphQueries &queries,
                            const BlockParameterJoin &join)
            {
                Instruction parameter = join.parameter();
                if(parameter.result_class() != ResultClass::ProgramValue ||
                   parameter.value_representation() !=
                       ValueRepresentation::TaggedValue)
                {
                    return BlockParameterRewrite::keep();
                }
                for(const Block *entry: queries.graph().entry_blocks())
                {
                    if(entry == &join.block())
                    {
                        return BlockParameterRewrite::keep();
                    }
                }

                std::vector<IncomingArgumentReplacement> replacements;
                replacements.reserve(join.block().predecessor_edges().size());
                for(IncomingArgument incoming: join.incoming_arguments())
                {
                    std::optional<F64Ref> source =
                        identity_discardable_box_source(
                            context, queries, incoming, parameter.id());
                    if(!source.has_value())
                    {
                        return BlockParameterRewrite::keep();
                    }
                    replacements.push_back(
                        IncomingArgumentReplacement{incoming.edge, *source});
                }
                if(replacements.empty())
                {
                    return BlockParameterRewrite::keep();
                }

                ParameterF64Instruction replacement_parameter =
                    context.make_instruction<ParameterF64Instruction>();
                BoxF64Instruction destination_box =
                    context.make_instruction<BoxF64Instruction>(
                        F64Ref(replacement_parameter));
                return BlockParameterRewrite::convert_representation(
                    replacement_parameter, replacements,
                    RewriteInsertion::insert({destination_box}),
                    ProgramValueRef(destination_box));
            }
        };

        RewriteSummary convert_f64_block_parameters(CompilationSession &session,
                                                    ControlFlowGraph &graph)
        {
            GraphRewriter rewriter(session, graph);
            F64BlockParameterRewrite rewrite;
            return rewriter.rewrite_instructions(
                InstructionTraversal().with_queries(GraphQuery::Uses), rewrite);
        }
    }  // namespace

    Result<bool, JitCompilationError>
    simplify_f64_boxing(CompilationSession &session, ControlFlowGraph &graph)
    {
        RewriteSummary local_before = simplify_local_f64_boxing(session, graph);
        RewriteSummary joins = convert_f64_block_parameters(session, graph);
        RewriteSummary local_after;
        if(joins.block_parameters_changed)
        {
            local_after = simplify_local_f64_boxing(session, graph);
        }
        return Result<bool, JitCompilationError>::ok(
            local_before.instructions_changed ||
            joins.block_parameters_changed || local_after.instructions_changed);
    }

}  // namespace cl::jit
