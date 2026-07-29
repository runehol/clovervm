#ifndef CL_JIT_AARCH64_BACKEND_H
#define CL_JIT_AARCH64_BACKEND_H

#include "jit/code_cache.h"
#include "jit/register_allocator.h"

#include <variant>

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;
    class JitCompilationObserver;

    using AArch64CompilationError =
        std::variant<RegisterAllocationError, JitCodeError>;

    [[nodiscard]] Result<PublishedCode, AArch64CompilationError>
    compile_to_aarch64(CompilationSession &session, ControlFlowGraph &graph,
                       CodeCache &cache, MachineAddress side_exit_thunk,
                       JitCompilationObserver *observer = nullptr);

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_BACKEND_H
