#ifndef CL_JIT_AARCH64_CFG_EMITTER_H
#define CL_JIT_AARCH64_CFG_EMITTER_H

#include "jit/code_cache.h"

namespace cl::jit
{
    class AArch64MacroAssembler;
    class ControlFlowGraph;
    class LocationAssignments;

    void generate_aarch64_assembly(const ControlFlowGraph &graph,
                                   const LocationAssignments &locations,
                                   AArch64MacroAssembler &assembler,
                                   MachineAddress side_exit_thunk);

    [[nodiscard]] Result<PublishedCode, JitCodeError>
    emit_aarch64_from_cfg(const ControlFlowGraph &graph,
                          const LocationAssignments &locations,
                          CodeCache &cache, MachineAddress side_exit_thunk);

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_CFG_EMITTER_H
