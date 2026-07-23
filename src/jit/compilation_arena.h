#ifndef CL_JIT_COMPILATION_ARENA_H
#define CL_JIT_COMPILATION_ARENA_H

#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/instruction_pool.h"
#include "jit/object_pool.h"

#include <span>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace cl::jit
{
    class RewriteContext;

    class CompilationArena
    {
    public:
        CompilationArena() = default;

        CompilationArena(const CompilationArena &) = delete;
        CompilationArena &operator=(const CompilationArena &) = delete;
        CompilationArena(CompilationArena &&) = delete;
        CompilationArena &operator=(CompilationArena &&) = delete;

    private:
        friend class GraphBuilder;
        friend class RewriteContext;

        template <typename... Args> Block *make_block(Args &&...args)
        {
            return blocks_.make(std::forward<Args>(args)...);
        }

        template <typename... Args> BlockEdge *make_block_edge(Args &&...args)
        {
            return block_edges_.make(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        T *make_instruction(Args &&...args)
        {
            static_assert(std::is_base_of_v<Instruction, T>);
            if constexpr(T::IsVariadic)
            {
                size_t n_indirect_slots = T::n_indirect_slots_for(args...);
                std::span<Instruction::Slot> indirect_slots =
                    instruction_side_data_.allocate_words(n_indirect_slots);
                return instructions_.make<T>(indirect_slots,
                                             std::forward<Args>(args)...);
            }
            else
            {
                return instructions_.make<T>(std::forward<Args>(args)...);
            }
        }

        ControlFlowGraph *make_graph() { return graphs_.make(); }

        ObjectPool<ControlFlowGraph> graphs_;
        ObjectPool<Block> blocks_;
        ObjectPool<BlockEdge> block_edges_;
        InstructionPool instructions_;
        InstructionSideDataPool instruction_side_data_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_ARENA_H
