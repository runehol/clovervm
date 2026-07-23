#include "jit/graph_queries.h"

#include "jit/use_lists.h"
#include "runtime/fatal.h"

namespace cl::jit
{
    const ControlFlowGraph &GraphQueries::graph() const { return *graph_; }

    const Uses &GraphQueries::uses_of(const Instruction &def) const
    {
        if(!has_graph_query(requested_, GraphQuery::Uses) ||
           use_lists_ == nullptr)
        {
            fatal("JIT use lists were accessed without being requested");
        }
        return use_lists_->uses_of(def);
    }

}  // namespace cl::jit
