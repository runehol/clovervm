#include "bytecode/bytecode_decoder.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>

namespace cl
{
    static int16_t read_int16_le(const uint8_t *p)
    {
        uint16_t raw = uint16_t(p[0]) | (uint16_t(p[1]) << 8);
        return int16_t(raw);
    }

    static bool is_operator(BytecodeCompoundRole role)
    {
        return role == BytecodeCompoundRole::BinaryOperator ||
               role == BytecodeCompoundRole::TernaryOperator;
    }

    static bool is_operator_continuation(BytecodeCompoundRole role)
    {
        return role == BytecodeCompoundRole::BinaryOperatorContinuation ||
               role == BytecodeCompoundRole::TernaryOperatorContinuation;
    }

    static Bytecode expected_operator_continuation(BytecodeCompoundRole role)
    {
        assert(is_operator(role));
        return role == BytecodeCompoundRole::BinaryOperator
                   ? Bytecode::CheckOperatorNotImplemented
                   : Bytecode::CheckTernaryOperatorNotImplemented;
    }

    static uint32_t jump_target_offset(const CodeObject &code_object,
                                       uint32_t pc_offset,
                                       const BytecodeInfo &info)
    {
        int8_t operand_offset = info.relative_jump_operand_offset();
        assert(operand_offset >= 0);
        uint32_t displacement_offset = pc_offset + uint32_t(operand_offset);
        assert(displacement_offset + 2 <= code_object.size());
        int32_t displacement =
            read_int16_le(&code_object.code[displacement_offset]);
        int64_t target = int64_t(displacement_offset) + 2 + displacement;
        assert(target >= 0);
        assert(target < int64_t(code_object.size()));
        return uint32_t(target);
    }

    BytecodeInstruction BytecodeInstructionIterator::operator*() const
    {
        assert(pc_offset_ < end_pc_offset_);
        return decoder_->decode_instruction_at(pc_offset_);
    }

    BytecodeInstructionIterator &BytecodeInstructionIterator::operator++()
    {
        assert(pc_offset_ < end_pc_offset_);
        pc_offset_ = decoder_->next_instruction_offset(pc_offset_);
        assert(pc_offset_ <= end_pc_offset_);
        return *this;
    }

    BytecodeDecoder::BytecodeDecoder(const CodeObject &code_object)
        : code_object_(code_object), inline_caches_(code_object.inline_caches)
    {
        build_blocks();
    }

    void BytecodeDecoder::build_blocks()
    {
        assert(code_object_.size() <= std::numeric_limits<uint32_t>::max());
        uint32_t code_size = uint32_t(code_object_.size());
        next_instruction_offsets_.assign(code_size,
                                         std::numeric_limits<uint32_t>::max());

        if(code_size == 0)
        {
            assert(code_object_.exception_table.empty());
            return;
        }

        std::vector<bool> semantic_boundaries(code_size + 1, false);
        semantic_boundaries[code_size] = true;

        for(uint32_t pc_offset = 0; pc_offset < code_size;)
        {
            semantic_boundaries[pc_offset] = true;
            Bytecode opcode = Bytecode(code_object_.code[pc_offset]);
            assert(is_valid_bytecode(opcode));
            const BytecodeInfo &info = bytecode_info(opcode);
            assert(!is_operator_continuation(info.compound_role));

            uint32_t next_pc_offset = pc_offset + info.length();
            assert(next_pc_offset <= code_size);
            if(is_operator(info.compound_role))
            {
                assert(next_pc_offset < code_size);
                Bytecode continuation =
                    Bytecode(code_object_.code[next_pc_offset]);
                assert(continuation ==
                       expected_operator_continuation(info.compound_role));
                next_pc_offset += bytecode_length(continuation);
                assert(next_pc_offset <= code_size);
            }

            next_instruction_offsets_[pc_offset] = next_pc_offset;
            pc_offset = next_pc_offset;
        }

        std::vector<bool> leaders(code_size + 1, false);
        leaders[0] = true;

        for(uint32_t pc_offset = 0; pc_offset < code_size;
            pc_offset = next_instruction_offset(pc_offset))
        {
            Bytecode opcode = Bytecode(code_object_.code[pc_offset]);
            const BytecodeInfo &info = bytecode_info(opcode);
            uint32_t next_pc_offset = next_instruction_offset(pc_offset);

            if(info.control_flow == BytecodeControlFlow::ConditionalJump ||
               info.control_flow == BytecodeControlFlow::UnconditionalJump)
            {
                uint32_t target =
                    jump_target_offset(code_object_, pc_offset, info);
                assert(semantic_boundaries[target]);
                leaders[target] = true;
            }

            if((info.control_flow == BytecodeControlFlow::ConditionalJump ||
                info.control_flow == BytecodeControlFlow::UnconditionalJump ||
                info.control_flow == BytecodeControlFlow::Terminator) &&
               next_pc_offset < code_size)
            {
                leaders[next_pc_offset] = true;
            }
        }

        for(const ExceptionTableEntry &entry: code_object_.exception_table)
        {
            assert(entry.start_pc < entry.end_pc);
            assert(entry.end_pc <= code_size);
            assert(entry.handler_pc < code_size);
            assert(semantic_boundaries[entry.start_pc]);
            assert(semantic_boundaries[entry.end_pc]);
            assert(semantic_boundaries[entry.handler_pc]);
            leaders[entry.start_pc] = true;
            leaders[entry.end_pc] = true;
            leaders[entry.handler_pc] = true;
        }

        std::vector<uint32_t> block_starts;
        for(uint32_t pc_offset = 0; pc_offset < code_size; ++pc_offset)
        {
            if(leaders[pc_offset])
            {
                assert(semantic_boundaries[pc_offset]);
                block_starts.push_back(pc_offset);
            }
        }

        std::vector<BytecodeBlockId> block_at_offset(
            code_size, std::numeric_limits<BytecodeBlockId>::max());
        blocks_.reserve(block_starts.size());
        for(size_t idx = 0; idx < block_starts.size(); ++idx)
        {
            uint32_t start = block_starts[idx];
            uint32_t end = idx + 1 < block_starts.size() ? block_starts[idx + 1]
                                                         : code_size;
            BytecodeBlockId id = BytecodeBlockId(idx);
            block_at_offset[start] = id;
            blocks_.push_back(BytecodeBlock(this, id, start, end));
        }

        auto add_successor = [&](BytecodeBlock &block,
                                 BytecodeBlockId successor) {
            if(std::find(block.successors_.begin(), block.successors_.end(),
                         successor) == block.successors_.end())
            {
                block.successors_.push_back(successor);
            }
        };

        for(BytecodeBlock &block: blocks_)
        {
            uint32_t last_pc_offset = block.start_pc_offset_;
            for(uint32_t pc_offset = block.start_pc_offset_;
                pc_offset < block.end_pc_offset_;
                pc_offset = next_instruction_offset(pc_offset))
            {
                last_pc_offset = pc_offset;
            }
            assert(next_instruction_offset(last_pc_offset) ==
                   block.end_pc_offset_);

            Bytecode opcode = Bytecode(code_object_.code[last_pc_offset]);
            const BytecodeInfo &info = bytecode_info(opcode);
            if(info.control_flow == BytecodeControlFlow::Fallthrough ||
               info.control_flow == BytecodeControlFlow::ConditionalJump)
            {
                if(block.end_pc_offset_ < code_size)
                {
                    BytecodeBlockId successor =
                        block_at_offset[block.end_pc_offset_];
                    assert(successor !=
                           std::numeric_limits<BytecodeBlockId>::max());
                    add_successor(block, successor);
                }
            }
            if(info.control_flow == BytecodeControlFlow::ConditionalJump ||
               info.control_flow == BytecodeControlFlow::UnconditionalJump)
            {
                uint32_t target =
                    jump_target_offset(code_object_, last_pc_offset, info);
                BytecodeBlockId successor = block_at_offset[target];
                assert(successor !=
                       std::numeric_limits<BytecodeBlockId>::max());
                add_successor(block, successor);
            }
        }

        for(BytecodeBlock &block: blocks_)
        {
            for(BytecodeBlockId successor: block.successors_)
            {
                blocks_[successor].predecessors_.push_back(block.id_);
            }

            for(size_t entry_idx = 0;
                entry_idx < code_object_.exception_table.size(); ++entry_idx)
            {
                const ExceptionTableEntry &entry =
                    code_object_.exception_table[entry_idx];
                if(!block.exception_handler_index_.has_value() &&
                   block.start_pc_offset_ >= entry.start_pc &&
                   block.start_pc_offset_ < entry.end_pc)
                {
                    block.exception_handler_index_ = uint32_t(entry_idx);
                }
                if(block.start_pc_offset_ == entry.handler_pc)
                {
                    block.exception_entrances_.push_back(uint32_t(entry_idx));
                }
            }
        }
    }

    BytecodeInstruction
    BytecodeDecoder::decode_instruction_at(uint32_t pc_offset) const
    {
        BytecodeInstruction instruction =
            cl::decode_instruction(code_object_, pc_offset);
        if(instruction.cache_.has_value())
        {
            attach_snapshot(*instruction.cache_);
        }
        if(instruction.cache2_.has_value())
        {
            attach_snapshot(*instruction.cache2_);
        }
        return instruction;
    }

    uint32_t BytecodeDecoder::next_instruction_offset(uint32_t pc_offset) const
    {
        assert(pc_offset < next_instruction_offsets_.size());
        uint32_t next = next_instruction_offsets_[pc_offset];
        assert(next != std::numeric_limits<uint32_t>::max());
        return next;
    }

    void BytecodeDecoder::attach_snapshot(InlineCacheReference &cache) const
    {
        switch(cache.kind)
        {
            case InlineCacheKind::AttributeRead:
                assert(cache.index <
                       inline_caches_.attribute_read_caches.size());
                cache.snapshot_ =
                    &inline_caches_.attribute_read_caches[cache.index];
                break;
            case InlineCacheKind::AttributeMutation:
                assert(cache.index <
                       inline_caches_.attribute_mutation_caches.size());
                cache.snapshot_ =
                    &inline_caches_.attribute_mutation_caches[cache.index];
                break;
            case InlineCacheKind::ModuleGlobalRead:
                assert(cache.index <
                       inline_caches_.module_global_read_caches.size());
                cache.snapshot_ =
                    &inline_caches_.module_global_read_caches[cache.index];
                break;
            case InlineCacheKind::ModuleGlobalMutation:
                assert(cache.index <
                       inline_caches_.module_global_mutation_caches.size());
                cache.snapshot_ =
                    &inline_caches_.module_global_mutation_caches[cache.index];
                break;
            case InlineCacheKind::FunctionCall:
                assert(cache.index <
                       inline_caches_.function_call_caches.size());
                cache.snapshot_ =
                    &inline_caches_.function_call_caches[cache.index];
                break;
            case InlineCacheKind::KeywordCall:
                assert(cache.index < inline_caches_.keyword_call_caches.size());
                cache.snapshot_ =
                    &inline_caches_.keyword_call_caches[cache.index];
                break;
            case InlineCacheKind::Operator:
                assert(cache.index < inline_caches_.operator_caches.size());
                cache.snapshot_ = &inline_caches_.operator_caches[cache.index];
                break;
        }
    }

}  // namespace cl
