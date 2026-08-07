#include "jit/aarch64_link_register_preservation.h"

#include "jit/aarch64_call.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_rewriter.h"

#include <cassert>

namespace cl::jit
{
    RewriteSummary
    insert_aarch64_link_register_preservation(CompilationSession &session,
                                              ControlFlowGraph &graph)
    {
        assert(graph.is_published());
        assert(graph.ir_level() == IRLevel::Machine);

        bool has_call = false;
        for(const Block *block: graph.blocks())
        {
            for(Instruction instruction: block->instructions())
            {
                has_call |=
                    aarch64_call_properties(instruction.kind()).has_value();
            }
        }
        if(!has_call)
        {
            return {};
        }

        struct Callback
        {
            const Block *entry;

            RewriteInsertion at_block_entry(RewriteContext &context,
                                            const GraphQueries &,
                                            const Block &block)
            {
                if(&block != entry)
                {
                    return RewriteInsertion::none();
                }
                return RewriteInsertion::insert({context.make_instruction<
                    SaveLinkRegisterToFrameInstruction>()});
            }

            RewriteInsertion before_instruction(RewriteContext &context,
                                                const GraphQueries &,
                                                const Block &,
                                                const Instruction &instruction)
            {
                if(instruction.kind() != InstructionKind::Return &&
                   instruction.kind() != InstructionKind::BareReturn)
                {
                    return RewriteInsertion::none();
                }
                return RewriteInsertion::insert({context.make_instruction<
                    RestoreLinkRegisterFromFrameInstruction>()});
            }
        } callback{graph.entry_block()};

        GraphRewriter rewriter(session, graph);
        return rewriter.rewrite_instructions(
            InstructionTraversal(), RewriteInput::Normalized, callback);
    }

}  // namespace cl::jit
