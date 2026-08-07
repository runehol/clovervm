#ifndef CL_JIT_F64_BOX_SIMPLIFICATION_H
#define CL_JIT_F64_BOX_SIMPLIFICATION_H

#include "jit/jit_compilation_error.h"
#include "util/result.h"

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    [[nodiscard]] Result<bool, JitCompilationError>
    simplify_f64_boxing(CompilationSession &session, ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_F64_BOX_SIMPLIFICATION_H
