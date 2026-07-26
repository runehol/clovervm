#ifndef CL_JIT_COMPILATION_STORAGE_H
#define CL_JIT_COMPILATION_STORAGE_H

#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/instruction_side_data.h"
#include "jit/object_pool.h"

#include <span>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <new>
#include <type_traits>
#include <utility>

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

        Instruction *instruction(InstructionId id);
        const Instruction *instruction(InstructionId id) const;

    private:
        friend class CompilationSession;
        friend class GraphBuilder;
        friend class GraphRewriter;
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
        T *make_instruction(Args &&...args)
        {
            static_assert(std::is_base_of_v<Instruction, T>);
            static_assert(sizeof(T) == sizeof(Instruction));
            static_assert(alignof(T) == alignof(Instruction));
            static_assert(std::is_trivially_destructible_v<T>);

            InstructionId id = next_instruction_id();
            instructions_.emplace_back();
            void *storage = instructions_.back().storage();
            if constexpr(T::IsVariadic)
            {
                size_t n_indirect_slots = T::n_indirect_slots_for(args...);
                std::span<Instruction::Slot> indirect_slots =
                    instruction_side_data_.allocate_words(n_indirect_slots);
                return new(storage)
                    T(id, indirect_slots, std::forward<Args>(args)...);
            }
            else
            {
                return new(storage) T(id, std::forward<Args>(args)...);
            }
        }

        ControlFlowGraph *make_graph() { return graphs_.make(this); }

        class InstructionSlot
        {
        public:
            InstructionSlot() = default;

            InstructionSlot(const InstructionSlot &) = delete;
            InstructionSlot &operator=(const InstructionSlot &) = delete;
            InstructionSlot(InstructionSlot &&) = delete;
            InstructionSlot &operator=(InstructionSlot &&) = delete;

            void *storage() { return storage_; }

            Instruction *instruction()
            {
                return std::launder(reinterpret_cast<Instruction *>(storage_));
            }

            const Instruction *instruction() const
            {
                return std::launder(
                    reinterpret_cast<const Instruction *>(storage_));
            }

        private:
            alignas(Instruction) std::byte storage_[sizeof(Instruction)];
        };

        static_assert(sizeof(InstructionSlot) == sizeof(Instruction));
        static_assert(alignof(InstructionSlot) == alignof(Instruction));

        InstructionId next_instruction_id() const;

        ObjectPool<ControlFlowGraph> graphs_;
        ObjectPool<Block> blocks_;
        ObjectPool<BlockEdge> block_edges_;
        std::deque<InstructionSlot> instructions_;
        InstructionSideDataPool instruction_side_data_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_STORAGE_H
