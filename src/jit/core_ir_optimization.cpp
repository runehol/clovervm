#include "jit/core_ir_optimization.h"

#include "jit/dead_code_elimination.h"
#include "jit/equivalent_block_parameters.h"

#include <array>
#include <utility>

namespace cl::jit
{
    Result<bool, JitCompilationError>
    optimize_core_ir(CompilationSession &session, ControlFlowGraph &graph)
    {
        static constexpr std::array passes = {
            CoreIRPass{"dead-code-elimination", eliminate_dead_code},
            CoreIRPass{"equivalent-block-parameters",
                       collapse_equivalent_block_parameters},
        };
        bool changed = false;
        for(const CoreIRPass &pass: passes)
        {
            auto result = pass.run(session, graph);
            if(!result)
            {
                return propagate_failure(std::move(result));
            }
            changed |= std::move(result).value();
        }
        return Result<bool, JitCompilationError>::ok(changed);
    }

}  // namespace cl::jit
