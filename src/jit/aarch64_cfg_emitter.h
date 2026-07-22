#ifndef CL_JIT_AARCH64_CFG_EMITTER_H
#define CL_JIT_AARCH64_CFG_EMITTER_H

#include "jit/code_cache.h"

namespace cl::jit
{
    class AArch64MacroAssembler;
    class ControlFlowGraph;

    void generate_aarch64_assembly(const ControlFlowGraph &graph,
                                   AArch64MacroAssembler &assembler);

    [[nodiscard]] Result<JitCodeObject *, JitCodeError>
    emit_aarch64_from_cfg(const ControlFlowGraph &graph, CodeCache &cache);

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_CFG_EMITTER_H
