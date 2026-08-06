#ifndef CL_JIT_AARCH64_LINK_REGISTER_PRESERVATION_H
#define CL_JIT_AARCH64_LINK_REGISTER_PRESERVATION_H

#include "jit/graph_rewriter.h"

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    RewriteSummary
    insert_aarch64_link_register_preservation(CompilationSession &session,
                                              ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_LINK_REGISTER_PRESERVATION_H
