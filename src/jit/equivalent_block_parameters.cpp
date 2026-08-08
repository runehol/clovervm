#include "jit/equivalent_block_parameters.h"

#include "jit/block_parameter_join.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_rewriter.h"

#include <absl/container/flat_hash_map.h>

#include <optional>

namespace cl::jit
{
    namespace
    {
        using ParameterReplacements =
            absl::flat_hash_map<InstructionId, InstructionId>;

        class EquivalentBlockParameterRewrite
        {
        public:
            explicit EquivalentBlockParameterRewrite(
                const ParameterReplacements &replacements)
                : replacements_(&replacements)
            {
            }

            BlockParameterRewrite
            block_parameter(RewriteContext &context, const GraphQueries &,
                            const BlockParameterJoin &join)
            {
                Instruction parameter = join.parameter();
                auto replacement = replacements_->find(parameter.id());
                if(replacement == replacements_->end())
                {
                    return BlockParameterRewrite::keep();
                }
                return BlockParameterRewrite::
                    replace_with_destination_parameter(ProgramValueRef(
                        context.instruction(replacement->second)));
            }

        private:
            const ParameterReplacements *replacements_;
        };
    }  // namespace

    Result<bool, JitCompilationError>
    collapse_equivalent_block_parameters(CompilationSession &session,
                                         ControlFlowGraph &graph)
    {
        ParameterReplacements replacements;
        for(const Block *block: graph.blocks())
        {
            if(block == graph.normal_entry_block())
            {
                continue;
            }

            absl::flat_hash_map<InstructionId, InstructionId>
                representative_by_base;
            for(BlockParameterJoin join: graph.block_parameter_joins(*block))
            {
                Instruction parameter = join.parameter();
                std::optional<InstructionId> base;
                bool equivalent = true;
                for(IncomingArgument incoming: join.incoming_arguments())
                {
                    InstructionId argument = incoming.value.instruction_id();
                    if(argument == parameter.id())
                    {
                        continue;
                    }
                    if(!base.has_value())
                    {
                        base = argument;
                    }
                    else if(*base != argument)
                    {
                        equivalent = false;
                        break;
                    }
                }
                if(!equivalent || !base.has_value())
                {
                    continue;
                }

                auto [representative, inserted] =
                    representative_by_base.emplace(*base, parameter.id());
                if(!inserted)
                {
                    replacements.emplace(parameter.id(),
                                         representative->second);
                }
            }
        }

        if(replacements.empty())
        {
            return Result<bool, JitCompilationError>::ok(false);
        }

        GraphRewriter rewriter(session, graph);
        EquivalentBlockParameterRewrite rewrite(replacements);
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), rewrite);
        return Result<bool, JitCompilationError>::ok(
            summary.block_parameters_changed);
    }

}  // namespace cl::jit
