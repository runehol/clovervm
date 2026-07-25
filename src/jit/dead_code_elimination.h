#ifndef CL_JIT_DEAD_CODE_ELIMINATION_H
#define CL_JIT_DEAD_CODE_ELIMINATION_H

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    void eliminate_dead_code(CompilationSession &session,
                             ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_DEAD_CODE_ELIMINATION_H
