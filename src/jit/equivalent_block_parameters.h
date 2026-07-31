#ifndef CL_JIT_EQUIVALENT_BLOCK_PARAMETERS_H
#define CL_JIT_EQUIVALENT_BLOCK_PARAMETERS_H

#include "jit/jit_compilation_error.h"
#include "util/result.h"

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    [[nodiscard]] Result<bool, JitCompilationError>
    collapse_equivalent_block_parameters(CompilationSession &session,
                                         ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_EQUIVALENT_BLOCK_PARAMETERS_H
