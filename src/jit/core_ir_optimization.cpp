#include "jit/core_ir_optimization.h"

#include "jit/constant_folding.h"
#include "jit/dead_code_elimination.h"
#include "jit/equivalent_block_parameters.h"
#include "jit/f64_box_simplification.h"
#include "jit/tagged_value_guard_simplification.h"

#include <array>
#include <cstddef>
#include <span>
#include <utility>

namespace cl::jit
{
    namespace
    {
        Result<bool, JitCompilationError> run_core_ir_passes_with_round_limit(
            CompilationSession &session, ControlFlowGraph &graph,
            std::span<const CoreIRPass> passes, size_t maximum_rounds)
        {
            bool changed = false;
            for(size_t round = 0; round < maximum_rounds; ++round)
            {
                bool round_changed = false;
                for(const CoreIRPass &pass: passes)
                {
                    auto result = pass.run(session, graph);
                    if(!result)
                    {
                        return propagate_failure(std::move(result));
                    }
                    round_changed |= std::move(result).value();
                }
                if(!round_changed)
                {
                    break;
                }
                changed = true;
            }
            return Result<bool, JitCompilationError>::ok(changed);
        }
    }  // namespace

    Result<bool, JitCompilationError>
    optimize_core_ir(CompilationSession &session, ControlFlowGraph &graph)
    {
        static constexpr std::array passes = {
            CoreIRPass{"tagged-value-guard-simplification",
                       simplify_tagged_value_guards},
            CoreIRPass{"f64-box-simplification", simplify_f64_boxing},
            CoreIRPass{"constant-folding", fold_constants},
            CoreIRPass{"tagged-value-guard-simplification",
                       simplify_tagged_value_guards},
            CoreIRPass{"f64-box-simplification", simplify_f64_boxing},
            CoreIRPass{"equivalent-block-parameters",
                       collapse_equivalent_block_parameters},
            CoreIRPass{"dead-code-elimination", eliminate_dead_code},
        };
        constexpr size_t maximum_rounds = 8;
        return run_core_ir_passes_with_round_limit(session, graph, passes,
                                                   maximum_rounds);
    }

}  // namespace cl::jit
