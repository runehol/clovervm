#ifndef CL_JIT_CORE_IR_OPTIMIZATION_H
#define CL_JIT_CORE_IR_OPTIMIZATION_H

#include "jit/jit_compilation_error.h"
#include "util/result.h"

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    struct CoreIRPass
    {
        const char *name;
        Result<bool, JitCompilationError> (*run)(CompilationSession &,
                                                 ControlFlowGraph &);
    };

    [[nodiscard]] Result<bool, JitCompilationError>
    optimize_core_ir(CompilationSession &session, ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_CORE_IR_OPTIMIZATION_H
