#ifndef CL_JIT_COMPILATION_STORAGE_H
#define CL_JIT_COMPILATION_STORAGE_H

#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/instruction_side_data.h"
#include "jit/object_pool.h"

#include <span>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

namespace cl::jit
{
    class CompilationSession;
    class RewriteContext;

    class CompilationStorage
    {
    public:
        CompilationStorage(const CompilationStorage &) = delete;
        CompilationStorage &operator=(const CompilationStorage &) = delete;
        CompilationStorage(CompilationStorage &&) = delete;
        CompilationStorage &operator=(CompilationStorage &&) = delete;

        Instruction instruction(InstructionId id) const;

    private:
        friend class CompilationSession;
        friend class GraphBuilder;
        friend class GraphRewriter;
        friend class Instruction;
        friend class RewriteContext;

        CompilationStorage() = default;

        template <typename... Args> Block *make_block(Args &&...args)
        {
            return blocks_.make(std::forward<Args>(args)...);
        }

        template <typename... Args> BlockEdge *make_block_edge(Args &&...args)
        {
            return block_edges_.make(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        T make_instruction(Args &&...args)
        {
            static_assert(std::is_base_of_v<Instruction, T>);
            static_assert(sizeof(T) == sizeof(Instruction));

            InstructionId id = next_instruction_id();
            if constexpr(T::IsVariadic)
            {
                size_t n_indirect_slots = T::n_indirect_slots_for(args...);
                std::span<Instruction::Slot> indirect_slots =
                    instruction_side_data_.allocate_words(n_indirect_slots);
                instructions_.push_back(
                    T::make_entry(indirect_slots, std::forward<Args>(args)...));
            }
            else
            {
                instructions_.push_back(
                    T::make_entry(std::forward<Args>(args)...));
            }
            return T(this, id);
        }

        ControlFlowGraph *make_graph() { return graphs_.make(this); }

        InstructionId next_instruction_id() const;
        const InstructionEntry &instruction_entry(InstructionId id) const;
        void detach_instruction(InstructionId id);

        ObjectPool<ControlFlowGraph> graphs_;
        ObjectPool<Block> blocks_;
        ObjectPool<BlockEdge> block_edges_;
        std::vector<InstructionEntry> instructions_;
        InstructionSideDataPool instruction_side_data_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_STORAGE_H
