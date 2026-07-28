#include "jit/dead_code_elimination.h"

#include "jit/compilation_storage.h"
#include "jit/graph_rewriter.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <cassert>
#include <cstddef>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct BlockParameterPosition
        {
            const Block *block;
            size_t index;
        };

        bool instruction_can_be_eliminated(const Instruction &instruction)
        {
            return instruction.result_class() != ResultClass::None &&
                   instruction_kind_metadata(instruction.kind()).may_effects <
                       EffectProfile::PythonVisibleEffects;
        }

        class DeadCodeRewrite
        {
        public:
            DeadCodeRewrite(const ControlFlowGraph &graph,
                            const absl::flat_hash_set<InstructionId> &live)
                : graph_(&graph), live_(&live)
            {
            }

            BlockParameterRewrite block_parameter(RewriteContext &,
                                                  const GraphQueries &,
                                                  const Block &block, size_t,
                                                  const Instruction &parameter)
            {
                return &block == graph_->entry_block() ||
                               live_->contains(parameter.id())
                           ? BlockParameterRewrite::keep()
                           : BlockParameterRewrite::erase();
            }

            RewriteResult rewrite_instruction(RewriteContext &,
                                              const GraphQueries &,
                                              const Block &,
                                              const Instruction &instruction)
            {
                return instruction_can_be_eliminated(instruction) &&
                               !live_->contains(instruction.id())
                           ? RewriteResult::erase()
                           : RewriteResult::keep();
            }

        private:
            const ControlFlowGraph *graph_;
            const absl::flat_hash_set<InstructionId> *live_;
        };
    }  // namespace

    Result<bool, JitCompilationError>
    eliminate_dead_code(CompilationSession &session, ControlFlowGraph &graph)
    {
        absl::flat_hash_map<InstructionId, BlockParameterPosition>
            block_parameters;
        for(const Block *block: graph.blocks())
        {
            for(size_t index = 0; index < block->parameters().size(); ++index)
            {
                block_parameters.emplace(block->parameter_at(index).id(),
                                         BlockParameterPosition{block, index});
            }
        }

        absl::flat_hash_set<InstructionId> live;
        std::vector<InstructionId> worklist;
        auto mark_live = [&](InstructionId instruction) {
            if(live.insert(instruction).second)
            {
                worklist.push_back(instruction);
            }
        };
        for(const Block *block: graph.blocks())
        {
            for(Instruction instruction: block->instructions())
            {
                if(!instruction_can_be_eliminated(instruction))
                {
                    mark_live(instruction.id());
                }
            }
        }

        while(!worklist.empty())
        {
            InstructionId instruction_id = worklist.back();
            worklist.pop_back();

            auto parameter = block_parameters.find(instruction_id);
            if(parameter != block_parameters.end())
            {
                const BlockParameterPosition &position = parameter->second;
                for(const BlockEdge *edge: position.block->predecessor_edges())
                {
                    assert(position.index < edge->arguments().size());
                    mark_live(
                        edge->arguments()[position.index].instruction_id());
                }
                continue;
            }

            Instruction instruction =
                graph.storage()->instruction(instruction_id);
            visit_operand_references(
                instruction,
                [&](uint32_t, OperandClass, ValueRepresentationRequirement,
                    InstructionId definition) { mark_live(definition); });
        }

        GraphRewriter rewriter(session, graph);
        DeadCodeRewrite rewrite(graph, live);
        RewriteSummary summary =
            rewriter.rewrite_instructions(InstructionTraversal(), rewrite);
        return Result<bool, JitCompilationError>::ok(
            summary.block_parameters_changed || summary.instructions_changed);
    }

}  // namespace cl::jit
