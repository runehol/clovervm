#ifndef CL_JIT_COMPILATION_ARENA_H
#define CL_JIT_COMPILATION_ARENA_H

#include "jit/core_ir.h"
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

        template <typename... Args> CoreBlock *make_core_block(Args &&...args)
        {
            return core_blocks_.make(std::forward<Args>(args)...);
        }

        template <typename Instruction, typename... Args>
        Instruction *make_core_instruction(Args &&...args)
        {
            static_assert(std::is_base_of_v<CoreInstruction, Instruction>);
            return core_instructions_.make<Instruction>(
                std::forward<Args>(args)...);
        }

    private:
        ObjectPool<CoreBlock> core_blocks_;
        PolymorphicObjectPool<CoreInstruction> core_instructions_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_ARENA_H
