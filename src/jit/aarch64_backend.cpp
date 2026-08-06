#include "jit/aarch64_backend.h"

#include "jit/aarch64_allocation_constraints.h"
#include "jit/aarch64_cfg_emitter.h"
#include "jit/jit_compiler.h"
#include "jit/side_exit_lowering.h"

#include <cassert>
#include <utility>

namespace cl::jit
{
    Result<AArch64CompiledCode, AArch64CompilationError>
    compile_to_aarch64(CompilationSession &session, ControlFlowGraph &graph,
                       CodeCache &cache, MachineAddress side_exit_thunk,
                       JitCompilationObserver *observer)
    {
        assert(graph.ir_level() == IRLevel::Core);
        SunkInstructionIds sunk_instructions = sink_snapshots(graph);
        auto lowering = lower_side_exits(session, graph, sunk_instructions);
        if(!lowering)
        {
            return Result<AArch64CompiledCode, AArch64CompilationError>::error(
                std::move(lowering).error());
        }
        assert(graph.ir_level() == IRLevel::Machine);

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(graph);
        auto materialized_result =
            allocate_registers(session, graph, constraints);
        if(!materialized_result)
        {
            return Result<AArch64CompiledCode, AArch64CompilationError>::error(
                std::move(materialized_result).error());
        }
        MaterializedAllocation materialized =
            std::move(materialized_result).value();
        if(observer != nullptr)
        {
            observer->on_machine_ir(graph);
        }

        auto emission = emit_aarch64_from_cfg(graph, materialized.locations(),
                                              cache, side_exit_thunk);
        if(!emission)
        {
            return Result<AArch64CompiledCode, AArch64CompilationError>::error(
                std::move(emission).error());
        }
        return Result<AArch64CompiledCode, AArch64CompilationError>::ok(
            {std::move(emission).value(),
             materialized.managed_frame_spill_extent()});
    }

}  // namespace cl::jit
