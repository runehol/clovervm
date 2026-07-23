#include "jit/graph_queries.h"

#include "jit/use_lists.h"

#include <cassert>

namespace cl::jit
{
    const ControlFlowGraph &GraphQueries::graph() const { return *graph_; }

    const Uses &GraphQueries::uses_of(const Instruction &def) const
    {
        assert(has_graph_query(requested_, GraphQuery::Uses));
        assert(use_lists_ != nullptr);
        return use_lists_->uses_of(def);
    }

}  // namespace cl::jit
