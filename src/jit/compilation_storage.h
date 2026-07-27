#ifndef CL_JIT_COMPILATION_STORAGE_H
#define CL_JIT_COMPILATION_STORAGE_H

#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/instruction_operand_table.h"
#include "jit/object_pool.h"

#include <span>

#include <cstddef>
#include <cstdint>
#include <deque>
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
        BlockEdge *block_edge(BlockEdgeId id) const;

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

        BlockEdge *
        make_block_edge(Block *source, Block *target,
                        std::span<const ProgramValueRef> arguments = {});

        template <typename T, typename... Args>
        T make_instruction(Args &&...args)
        {
            static_assert(std::is_base_of_v<Instruction, T>);
            static_assert(sizeof(T) == sizeof(Instruction));

            InstructionId id = next_instruction_id();
            if constexpr(T::OperandsAreIndirect)
            {
                size_t n_indirect_slots = T::n_indirect_slots_for(args...);
                InstructionOperandTable::Allocation indirect =
                    instruction_operands_.allocate(n_indirect_slots);
                instructions_.push_back(
                    T::make_entry(indirect.offset, indirect.words,
                                  std::forward<Args>(args)...));
            }
            else
            {
                instructions_.push_back(
                    T::make_entry(std::forward<Args>(args)...));
            }
            return T(this, id);
        }

        ControlFlowGraph *make_graph(IRLevel ir_level)
        {
            return graphs_.make(this, ir_level);
        }

        InstructionId next_instruction_id() const;
        BlockEdgeId next_block_edge_id() const;
        const InstructionEntry &instruction_entry(InstructionId id) const;
        std::span<const Instruction::Slot>
        instruction_operands(uint32_t offset, size_t count) const;
        void detach_instruction(InstructionId id);

        ObjectPool<ControlFlowGraph> graphs_;
        ObjectPool<Block> blocks_;
        std::deque<BlockEdge> block_edges_;
        std::vector<InstructionEntry> instructions_;
        InstructionOperandTable instruction_operands_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_STORAGE_H
