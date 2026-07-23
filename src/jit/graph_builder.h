#ifndef CL_JIT_GRAPH_BUILDER_H
#define CL_JIT_GRAPH_BUILDER_H

#include "jit/compilation_session.h"
#include "jit/instruction.h"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <utility>

namespace cl::jit
{
    template <typename T>
    concept ParameterInstructionType = std::same_as<T, ParameterInstruction> ||
                                       std::same_as<T, ParameterF64Instruction>;

    // Construction and rewriting APIs use one ownership vocabulary:
    //
    //   make     allocate an arena-owned object without attaching it;
    //   append   attach an existing object to the end of a specified container;
    //   emplace  allocate and attach to the end in one operation.
    //
    // Keeping these operations distinct lets callers prepare unpublished
    // objects before placement while making the common allocate-and-attach path
    // explicit. Published-graph rewriters follow the same convention.
    class GraphBuilder
    {
    public:
        explicit GraphBuilder(CompilationSession &session);

        GraphBuilder(const GraphBuilder &) = delete;
        GraphBuilder &operator=(const GraphBuilder &) = delete;
        GraphBuilder(GraphBuilder &&) = delete;
        GraphBuilder &operator=(GraphBuilder &&) = delete;

        Block *make_block();
        void append_block(Block *block);
        Block *emplace_block();
        void emplace_n_blocks(size_t count);

        Block *block_at(size_t index) const;
        size_t block_count() const;

        template <typename T, typename... Args>
        T *make_instruction(Args &&...args)
        {
            assert_can_build();
            return arena_->make_instruction<T>(std::forward<Args>(args)...);
        }

        void append_instruction(Block *block, Instruction *instruction);

        template <typename T, typename... Args>
        T *emplace_instruction(Block *block, Args &&...args)
        {
            T *instruction = make_instruction<T>(std::forward<Args>(args)...);
            append_instruction(block, instruction);
            return instruction;
        }

        template <ParameterInstructionType T>
        void append_parameter(Block *block, T *parameter)
        {
            assert_can_mutate(block);
            assert(block == graph_->entry_block());
            assert(parameter != nullptr);
            block->append_parameter(parameter);
        }

        template <ParameterInstructionType T, typename... Args>
        T *emplace_parameter(Block *block, Args &&...args)
        {
            T *parameter = make_instruction<T>(std::forward<Args>(args)...);
            append_parameter(block, parameter);
            return parameter;
        }

        BlockEdge *make_block_edge(Block *source, Block *target);

        ControlFlowGraph *finalize();

    private:
        void build_predecessor_edges();
        void assert_can_build() const;
        void assert_can_mutate(const Block *block) const;

        CompilationArena *arena_;
        ControlFlowGraph *graph_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_GRAPH_BUILDER_H
