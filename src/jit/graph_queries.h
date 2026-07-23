#ifndef CL_JIT_GRAPH_QUERIES_H
#define CL_JIT_GRAPH_QUERIES_H

#include "jit/control_flow_graph.h"

#include <cstdint>

namespace cl::jit
{
    class Uses;
    class UseLists;

    enum class GraphQuery : uint8_t
    {
        None = 0,
        Uses = 1 << 0,
    };

    constexpr GraphQuery operator|(GraphQuery lhs, GraphQuery rhs)
    {
        return static_cast<GraphQuery>(static_cast<uint8_t>(lhs) |
                                       static_cast<uint8_t>(rhs));
    }

    constexpr bool has_graph_query(GraphQuery queries, GraphQuery query)
    {
        return (static_cast<uint8_t>(queries) & static_cast<uint8_t>(query)) ==
               static_cast<uint8_t>(query);
    }

    class GraphQueries
    {
    public:
        const ControlFlowGraph &graph() const;
        const Uses &uses_of(const Instruction &def) const;

    private:
        friend class ControlFlowGraph;

        GraphQueries(const ControlFlowGraph *graph, GraphQuery requested,
                     const UseLists *use_lists)
            : graph_(graph), requested_(requested), use_lists_(use_lists)
        {
        }

        const ControlFlowGraph *graph_;
        GraphQuery requested_;
        const UseLists *use_lists_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_GRAPH_QUERIES_H
