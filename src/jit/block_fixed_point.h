#ifndef CL_JIT_BLOCK_FIXED_POINT_H
#define CL_JIT_BLOCK_FIXED_POINT_H

#include "jit/block_order.h"
#include "jit/deduplicating_queue.h"

#include <absl/container/flat_hash_set.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace cl::jit
{
    enum class DataflowUpdate : uint8_t
    {
        Unchanged,
        Changed,
    };

    enum class FixedPointStatus : uint8_t
    {
        Converged,
        RevisitLimitReached,
    };

    namespace detail
    {
        template <typename Callback>
        void for_each_dataflow_dependant(const Block &block, BlockOrder order,
                                         Callback &&callback)
        {
            switch(order)
            {
                case BlockOrder::Forward:
                    for(const BlockEdge *edge: block.block_successor_edges())
                    {
                        std::invoke(callback, edge->target());
                    }
                    return;
                case BlockOrder::Backward:
                    for(const BlockEdge *edge: block.predecessor_edges())
                    {
                        std::invoke(callback, edge->source());
                    }
                    return;
                case BlockOrder::Program:
                    assert(false);
                    return;
            }
        }
    }  // namespace detail

    template <typename Callback>
    FixedPointStatus iterate_blocks_to_fixed_point(
        const ControlFlowGraph &graph, BlockOrder order,
        size_t maximum_total_revisits, Callback &&callback)
    {
        assert(order != BlockOrder::Program);
        DeduplicatingQueue<const Block *> revisit_queue;
        absl::flat_hash_set<const Block *> visited;
        visited.reserve(graph.blocks().size());

        for(const Block *block: ordered_blocks(graph, order))
        {
            visited.insert(block);
            if(std::invoke(callback, *block) == DataflowUpdate::Unchanged)
            {
                continue;
            }
            detail::for_each_dataflow_dependant(
                *block, order, [&](const Block *dependant) {
                    if(visited.contains(dependant))
                    {
                        revisit_queue.enqueue(dependant);
                    }
                });
        }

        size_t total_revisits = 0;
        while(!revisit_queue.empty())
        {
            if(total_revisits == maximum_total_revisits)
            {
                return FixedPointStatus::RevisitLimitReached;
            }

            const Block *block = revisit_queue.dequeue();
            ++total_revisits;
            if(std::invoke(callback, *block) == DataflowUpdate::Unchanged)
            {
                continue;
            }
            detail::for_each_dataflow_dependant(
                *block, order, [&](const Block *dependant) {
                    revisit_queue.enqueue(dependant);
                });
        }

        return FixedPointStatus::Converged;
    }

}  // namespace cl::jit

#endif  // CL_JIT_BLOCK_FIXED_POINT_H
