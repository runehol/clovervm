#include "jit/aarch64_backend.h"

#include "jit/aarch64_allocation_constraints.h"
#include "jit/aarch64_cfg_emitter.h"
#include "jit/dead_code_elimination.h"

#include <utility>

namespace cl::jit
{
    Result<PublishedCode, AArch64CompilationError>
    compile_to_aarch64(CompilationSession &session, ControlFlowGraph &graph,
                       CodeCache &cache)
    {
        eliminate_dead_code(session, graph);
        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(graph);
        auto locations_result = allocate_registers(session, graph, constraints);
        if(!locations_result)
        {
            return Result<PublishedCode, AArch64CompilationError>::error(
                std::move(locations_result).error());
        }
        LocationAssignments locations = std::move(locations_result).value();

        auto emission = emit_aarch64_from_cfg(graph, locations, cache);
        if(!emission)
        {
            return Result<PublishedCode, AArch64CompilationError>::error(
                std::move(emission).error());
        }
        return Result<PublishedCode, AArch64CompilationError>::ok(
            std::move(emission).value());
    }

}  // namespace cl::jit
