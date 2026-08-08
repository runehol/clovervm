#ifndef CL_JIT_CONSTANT_FOLDING_H
#define CL_JIT_CONSTANT_FOLDING_H

#include "jit/jit_compilation_error.h"
#include "util/result.h"

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    [[nodiscard]] Result<bool, JitCompilationError>
    fold_constants(CompilationSession &session, ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_CONSTANT_FOLDING_H
