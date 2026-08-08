#include "jit/block_order.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <unordered_set>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct DepthFirstFrame
        {
            const Block *block;
            size_t next_successor = 0;
        };

        void append_component_reverse_postorder(
            const Block *root, std::unordered_set<const Block *> &visited,
            std::vector<const Block *> &result)
        {
            if(!visited.insert(root).second)
            {
                return;
            }

            std::vector<const Block *> postorder;
            std::vector<DepthFirstFrame> stack{{root}};
            while(!stack.empty())
            {
                DepthFirstFrame &frame = stack.back();
                TerminatorInstruction::BlockSuccessorEdges successors =
                    frame.block->block_successor_edges();
                if(frame.next_successor < successors.size())
                {
                    const Block *successor =
                        successors[frame.next_successor++]->target();
                    if(visited.insert(successor).second)
                    {
                        stack.push_back({successor});
                    }
                    continue;
                }

                postorder.push_back(frame.block);
                stack.pop_back();
            }

            result.insert(result.end(), postorder.rbegin(), postorder.rend());
        }

        std::vector<const Block *> forward_order(const ControlFlowGraph &graph)
        {
            std::vector<const Block *> result;
            result.reserve(graph.blocks().size());
            std::unordered_set<const Block *> visited;
            visited.reserve(graph.blocks().size());

            for(const Block *entry: graph.entry_blocks())
            {
                append_component_reverse_postorder(entry, visited, result);
            }
            for(const Block *block: graph.blocks())
            {
                append_component_reverse_postorder(block, visited, result);
            }

            assert(result.size() == graph.blocks().size());
            return result;
        }
    }  // namespace

    std::vector<const Block *> ordered_blocks(const ControlFlowGraph &graph,
                                              BlockOrder order)
    {
        assert(graph.is_published());
        switch(order)
        {
            case BlockOrder::Program:
                return {graph.blocks().begin(), graph.blocks().end()};
            case BlockOrder::Forward:
                return forward_order(graph);
            case BlockOrder::Backward:
                {
                    std::vector<const Block *> result = forward_order(graph);
                    std::reverse(result.begin(), result.end());
                    return result;
                }
        }
        assert(false);
        return {};
    }

}  // namespace cl::jit
