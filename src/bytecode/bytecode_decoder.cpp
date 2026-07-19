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

    static bool is_jump(BytecodeControlFlow control_flow)
    {
        return control_flow == BytecodeControlFlow::ConditionalJump ||
               control_flow == BytecodeControlFlow::UnconditionalJump;
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
        Bytecode opcode = Bytecode(decoder_->code_object_.code[pc_offset_]);
        pc_offset_ += bytecode_length(opcode);
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
        assert(code_size > 0);

        std::vector<bool> semantic_boundaries(code_size + 1, false);
        semantic_boundaries[code_size] = true;
        std::vector<bool> leaders(code_size + 1, false);
        std::vector<bool> exception_handler_offsets(code_size, false);
        leaders[0] = true;
        for(uint32_t pc_offset = 0; pc_offset < code_size;)
        {
            Bytecode opcode = Bytecode(code_object_.code[pc_offset]);
            assert(is_valid_bytecode(opcode));
            const BytecodeInfo &info = bytecode_info(opcode);
            uint32_t next_pc_offset = pc_offset + bytecode_length(opcode);
            assert(next_pc_offset <= code_size);
            semantic_boundaries[pc_offset] = true;

            if(is_jump(info.control_flow))
            {
                uint32_t target =
                    jump_target_offset(code_object_, pc_offset, info);
                leaders[target] = true;
            }

            if(info.control_flow != BytecodeControlFlow::Fallthrough &&
               next_pc_offset < code_size)
            {
                leaders[next_pc_offset] = true;
            }
            pc_offset = next_pc_offset;
        }

        for(const ExceptionTableEntry &entry: code_object_.exception_table)
        {
            assert(entry.handler_pc < code_size);
            assert(semantic_boundaries[entry.handler_pc]);
            leaders[entry.handler_pc] = true;
            exception_handler_offsets[entry.handler_pc] = true;
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
            if(exception_handler_offsets[start])
            {
                exception_handler_block_ids_.push_back(id);
            }
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
            uint32_t pc_offset = block.start_pc_offset_;
            for(; pc_offset < block.end_pc_offset_;)
            {
                Bytecode opcode = Bytecode(code_object_.code[pc_offset]);
                last_pc_offset = pc_offset;
                pc_offset += bytecode_length(opcode);
            }
            assert(pc_offset == block.end_pc_offset_);

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
            if(is_jump(info.control_flow))
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
        }
    }

    BytecodeInstruction
    BytecodeDecoder::decode_instruction_at(uint32_t pc_offset) const
    {
        BytecodeInstruction instruction =
            cl::decode_instruction(code_object_, pc_offset);
        instruction.inline_cache_tables_ = &inline_caches_;
        return instruction;
    }

}  // namespace cl
