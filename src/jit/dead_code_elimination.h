#ifndef CL_JIT_DEAD_CODE_ELIMINATION_H
#define CL_JIT_DEAD_CODE_ELIMINATION_H

#include "jit/jit_compilation_error.h"
#include "util/result.h"

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    [[nodiscard]] Result<bool, JitCompilationError>
    eliminate_dead_code(CompilationSession &session, ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_DEAD_CODE_ELIMINATION_H
