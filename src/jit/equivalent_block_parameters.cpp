#include "jit/equivalent_block_parameters.h"

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

            BlockParameterRewrite block_parameter(RewriteContext &context,
                                                  const GraphQueries &,
                                                  const Block &, size_t,
                                                  const Instruction &parameter)
            {
                auto replacement = replacements_->find(parameter.id());
                if(replacement == replacements_->end())
                {
                    return BlockParameterRewrite::keep();
                }
                return BlockParameterRewrite::replace_with(
                    ProgramValueRef(context.instruction(replacement->second)));
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
            if(block == graph.entry_block())
            {
                continue;
            }

            absl::flat_hash_map<InstructionId, InstructionId>
                representative_by_base;
            for(size_t index = 0; index < block->parameters().size(); ++index)
            {
                Instruction parameter = block->parameter_at(index);
                std::optional<InstructionId> base;
                bool equivalent = true;
                for(const BlockEdge *edge: block->predecessor_edges())
                {
                    InstructionId argument =
                        edge->arguments()[index].instruction_id();
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
