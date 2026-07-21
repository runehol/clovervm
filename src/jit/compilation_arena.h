#ifndef CL_JIT_COMPILATION_ARENA_H
#define CL_JIT_COMPILATION_ARENA_H

#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/instruction_pool.h"
#include "jit/object_pool.h"

#include <absl/types/span.h>

#include <cstdint>

namespace cl::jit
{
    class CompilationArena
    {
    public:
        CompilationArena() = default;

        CompilationArena(const CompilationArena &) = delete;
        CompilationArena &operator=(const CompilationArena &) = delete;
        CompilationArena(CompilationArena &&) = delete;
        CompilationArena &operator=(CompilationArena &&) = delete;

        template <typename... Args> Block *make_block(Args &&...args)
        {
            return blocks_.make(std::forward<Args>(args)...);
        }

        template <typename... Args> BlockEdge *make_block_edge(Args &&...args)
        {
            return block_edges_.make(std::forward<Args>(args)...);
        }

        ProgramValueRef make_parameter();
        ProgramValueRef make_synthesize_immediate(InlineValueConstant value);
        SnapshotRef make_snapshot(uint32_t resume_pc);
        ProgramValueRef make_add_smi(ProgramValueOperand lhs,
                                     ProgramValueOperand rhs,
                                     SnapshotRef snapshot);
        ProgramValueRef
        make_python_call(ProgramValueOperand callable,
                         absl::Span<const ProgramValueOperand> arguments,
                         SnapshotRef snapshot, uint32_t interpreter_return_pc);

        Instruction *make_conditional_branch(ProgramValueOperand condition,
                                             BlockEdge *true_edge,
                                             BlockEdge *false_edge);
        Instruction *make_unconditional_branch(BlockEdge *edge);
        Instruction *make_return(ProgramValueOperand value);

    private:
        Instruction *
        make_instruction(InstructionKind kind, uint16_t operand_count,
                         bool indirect_operands,
                         absl::Span<const Instruction::Slot> inline_slots)
        {
            return instructions_.make(kind, operand_count, indirect_operands,
                                      inline_slots);
        }

        ObjectPool<Block> blocks_;
        ObjectPool<BlockEdge> block_edges_;
        InstructionPool instructions_;
        InstructionSideDataPool instruction_side_data_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_ARENA_H
