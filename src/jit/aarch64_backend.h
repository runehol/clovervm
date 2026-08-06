#ifndef CL_JIT_AARCH64_BACKEND_H
#define CL_JIT_AARCH64_BACKEND_H

#include "jit/code_cache.h"
#include "jit/register_allocator.h"

#include <cstdint>
#include <variant>

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;
    class JitCompilationObserver;

    using AArch64CompilationError =
        std::variant<RegisterAllocationError, JitCodeError>;

    struct AArch64CompiledCode
    {
        PublishedCode code;
        uint32_t managed_frame_spill_extent;
    };

    [[nodiscard]] Result<AArch64CompiledCode, AArch64CompilationError>
    compile_to_aarch64(CompilationSession &session, ControlFlowGraph &graph,
                       CodeCache &cache, MachineAddress side_exit_thunk,
                       JitCompilationObserver *observer = nullptr);

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_BACKEND_H
