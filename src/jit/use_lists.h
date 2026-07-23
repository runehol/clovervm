#ifndef CL_JIT_USE_LISTS_H
#define CL_JIT_USE_LISTS_H

#include "jit/control_flow_graph.h"

#include <absl/container/flat_hash_map.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl::jit
{
    struct InstructionUse
    {
        const Instruction *instruction;
        uint16_t operand_index;
    };

    struct BlockArgumentUse
    {
        const BlockEdge *edge;
        uint16_t argument_index;
    };

    class Uses
    {
    public:
        const Instruction *def() const { return def_; }
        const Block *block() const { return block_; }

        ResultClass result_class() const { return def_->result_class(); }

        ValueRepresentation value_representation() const
        {
            return instruction_value_representation(def_->kind());
        }

        size_t n_uses() const
        {
            return instruction_uses_.size() + block_argument_uses_.size();
        }

        size_t n_instruction_uses() const { return instruction_uses_.size(); }

        size_t n_block_argument_uses() const
        {
            return block_argument_uses_.size();
        }

        const std::vector<InstructionUse> &instruction_uses() const
        {
            return instruction_uses_;
        }

        const std::vector<BlockArgumentUse> &block_argument_uses() const
        {
            return block_argument_uses_;
        }

    private:
        friend class UseLists;

        Uses(const Instruction *def, const Block *block)
            : def_(def), block_(block)
        {
        }

        const Instruction *def_;
        const Block *block_;
        std::vector<InstructionUse> instruction_uses_;
        std::vector<BlockArgumentUse> block_argument_uses_;
    };

    class UseLists
    {
    public:
        UseLists(const UseLists &) = delete;
        UseLists &operator=(const UseLists &) = delete;
        UseLists(UseLists &&) = default;
        UseLists &operator=(UseLists &&) = default;

        const Uses &uses_of(const Instruction &def) const;

    private:
        friend class ControlFlowGraph;

        explicit UseLists(const ControlFlowGraph &graph);

        uint64_t graph_generation() const { return graph_generation_; }
        void add_def(const Block &block, const Instruction &def);
        void add_instruction_uses(const Block &block,
                                  const Instruction &instruction);

        uint64_t graph_generation_;
        std::vector<Uses> uses_;
        absl::flat_hash_map<const Instruction *, size_t> index_by_def_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_USE_LISTS_H
