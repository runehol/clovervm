#ifndef CL_JIT_COMPILATION_STORAGE_H
#define CL_JIT_COMPILATION_STORAGE_H

#include "jit/control_flow_graph.h"
#include "jit/instruction.h"
#include "jit/instruction_attribute_pool.h"
#include "jit/instruction_operand_table.h"
#include "jit/object_pool.h"
#include "jit/side_exit_region.h"

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
        const SideExitRegion &side_exit_region(SideExitRegionId id) const;
        bool owns_side_exit_region(SideExitRegionId id) const
        {
            return id.value() < side_exit_regions_.size();
        }

    private:
        friend class CompilationSession;
        friend class GraphBuilder;
        friend class GraphRewriter;
        friend class Instruction;
        friend class RewriteContext;
        friend InstructionOperandAllocation
        allocate_instruction_operands(CompilationStorage &storage,
                                      size_t count);
        friend uint32_t
        store_instruction_value_attribute(CompilationStorage &storage,
                                          Value value);
        friend Value
        load_instruction_value_attribute(const CompilationStorage &storage,
                                         uint32_t index);
        friend uint32_t
        store_instruction_heap_object_attribute(CompilationStorage &storage,
                                                HeapObject *object);
        friend HeapObject *load_instruction_heap_object_attribute(
            const CompilationStorage &storage, uint32_t index);
        friend uint32_t store_instruction_function_pointer_attribute(
            CompilationStorage &storage, TrustedHandlerTarget target);
        friend TrustedHandlerTarget load_instruction_function_pointer_attribute(
            const CompilationStorage &storage, uint32_t index);

        CompilationStorage() = default;

        template <typename... Args> Block *make_block(Args &&...args)
        {
            return blocks_.make(std::forward<Args>(args)...);
        }

        BlockEdge *
        make_block_edge(Block *source, Block *target,
                        std::span<const ProgramValueRef> arguments = {});

        SideExitRegion *
        make_side_exit_region(std::span<const InstructionId> parameter_ids,
                              std::span<const InstructionId> instruction_ids);

        template <typename T, typename... Args>
        requires(!requires { T::Subkind; })
        T make_instruction(typename T::SubkindType subkind, Args &&...args)
        {
            static_assert(std::is_base_of_v<Instruction, T>);
            static_assert(sizeof(T) == sizeof(Instruction));

            InstructionId id = next_instruction_id();
            instructions_.push_back(
                T::make_entry(*this, subkind, std::forward<Args>(args)...));
            return T(this, id);
        }

        template <typename T, typename... Args>
        requires requires { T::Subkind; }
        T make_instruction(Args &&...args)
        {
            static_assert(std::is_base_of_v<Instruction, T>);
            static_assert(sizeof(T) == sizeof(Instruction));
            using Family = typename T::Family;

            InstructionId id = next_instruction_id();
            instructions_.push_back(Family::make_entry(
                *this, T::Subkind, std::forward<Args>(args)...));
            return T(this, id);
        }

        ControlFlowGraph *make_graph(IRLevel ir_level)
        {
            return graphs_.make(this, ir_level);
        }

        InstructionId next_instruction_id() const;
        BlockEdgeId next_block_edge_id() const;
        SideExitRegionId next_side_exit_region_id() const;
        const InstructionEntry &instruction_entry(InstructionId id) const;
        std::span<const Instruction::Slot>
        instruction_operands(uint32_t offset, size_t count) const;
        void poison_instruction(InstructionId id);

        ObjectPool<ControlFlowGraph> graphs_;
        ObjectPool<Block> blocks_;
        std::deque<BlockEdge> block_edges_;
        std::deque<SideExitRegion> side_exit_regions_;
        std::vector<InstructionEntry> instructions_;
        InstructionOperandTable instruction_operands_;
        InstructionAttributePool<Value> instruction_values_;
        InstructionAttributePool<HeapObject *> instruction_heap_objects_;
        InstructionAttributePool<TrustedHandlerTarget>
            instruction_function_pointers_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_STORAGE_H
