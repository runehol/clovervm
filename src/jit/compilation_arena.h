#ifndef CL_JIT_COMPILATION_ARENA_H
#define CL_JIT_COMPILATION_ARENA_H

#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/object_pool.h"

#include <type_traits>
#include <utility>

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

        template <typename InstructionType, typename... Args>
        InstructionType *make_instruction(Args &&...args)
        {
            static_assert(std::is_base_of_v<Instruction, InstructionType>);
            return instructions_.make<InstructionType>(
                std::forward<Args>(args)...);
        }

    private:
        ObjectPool<Block> blocks_;
        ObjectPool<BlockEdge> block_edges_;
        PolymorphicObjectPool<Instruction> instructions_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_ARENA_H
