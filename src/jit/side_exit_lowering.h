#ifndef CL_JIT_SIDE_EXIT_LOWERING_H
#define CL_JIT_SIDE_EXIT_LOWERING_H

#include "jit/instruction.h"
#include "jit/jit_compilation_error.h"
#include "util/result.h"

#include <absl/container/flat_hash_set.h>

namespace cl::jit
{
    class CompilationSession;
    class ControlFlowGraph;

    using SunkInstructionIds = absl::flat_hash_set<InstructionId>;

    SunkInstructionIds sink_snapshots(const ControlFlowGraph &graph);

    [[nodiscard]] Result<bool, JitCompilationError>
    lower_side_exits(CompilationSession &session, ControlFlowGraph &graph,
                     const SunkInstructionIds &sunk_instructions);

}  // namespace cl::jit

#endif  // CL_JIT_SIDE_EXIT_LOWERING_H
