#ifndef CL_BYTECODE_DECODER_H
#define CL_BYTECODE_DECODER_H

#include "bytecode/bytecode_instruction.h"
#include "bytecode/code_object.h"

#include <cstdint>
#include <vector>

namespace cl
{
    using BytecodeBlockId = uint32_t;

    class BytecodeDecoder;

    class BytecodeInstructionIterator
    {
    public:
        BytecodeInstruction operator*() const;
        BytecodeInstructionIterator &operator++();

        bool operator==(const BytecodeInstructionIterator &other) const
        {
            return decoder_ == other.decoder_ &&
                   pc_offset_ == other.pc_offset_ &&
                   end_pc_offset_ == other.end_pc_offset_;
        }

        bool operator!=(const BytecodeInstructionIterator &other) const
        {
            return !(*this == other);
        }

    private:
        friend class BytecodeInstructionRange;

        BytecodeInstructionIterator(const BytecodeDecoder *decoder,
                                    uint32_t pc_offset, uint32_t end_pc_offset)
            : decoder_(decoder), pc_offset_(pc_offset),
              end_pc_offset_(end_pc_offset)
        {
        }

        const BytecodeDecoder *decoder_;
        uint32_t pc_offset_;
        uint32_t end_pc_offset_;
    };

    class BytecodeInstructionRange
    {
    public:
        BytecodeInstructionIterator begin() const
        {
            return {decoder_, start_pc_offset_, end_pc_offset_};
        }

        BytecodeInstructionIterator end() const
        {
            return {decoder_, end_pc_offset_, end_pc_offset_};
        }

    private:
        friend class BytecodeBlock;

        BytecodeInstructionRange(const BytecodeDecoder *decoder,
                                 uint32_t start_pc_offset,
                                 uint32_t end_pc_offset)
            : decoder_(decoder), start_pc_offset_(start_pc_offset),
              end_pc_offset_(end_pc_offset)
        {
        }

        const BytecodeDecoder *decoder_;
        uint32_t start_pc_offset_;
        uint32_t end_pc_offset_;
    };

    class BytecodeBlock
    {
    public:
        BytecodeBlockId id() const { return id_; }
        uint32_t start_pc_offset() const { return start_pc_offset_; }
        uint32_t end_pc_offset() const { return end_pc_offset_; }

        const std::vector<BytecodeBlockId> &predecessors() const
        {
            return predecessors_;
        }

        const std::vector<BytecodeBlockId> &successors() const
        {
            return successors_;
        }

        BytecodeInstructionRange instructions() const
        {
            return {decoder_, start_pc_offset_, end_pc_offset_};
        }

    private:
        friend class BytecodeDecoder;

        BytecodeBlock(const BytecodeDecoder *decoder, BytecodeBlockId id,
                      uint32_t start_pc_offset, uint32_t end_pc_offset)
            : decoder_(decoder), id_(id), start_pc_offset_(start_pc_offset),
              end_pc_offset_(end_pc_offset)
        {
        }

        const BytecodeDecoder *decoder_;
        BytecodeBlockId id_;
        uint32_t start_pc_offset_;
        uint32_t end_pc_offset_;
        std::vector<BytecodeBlockId> predecessors_;
        std::vector<BytecodeBlockId> successors_;
    };

    class BytecodeDecoder
    {
    public:
        explicit BytecodeDecoder(const CodeObject &code_object);

        BytecodeDecoder(const BytecodeDecoder &) = delete;
        BytecodeDecoder &operator=(const BytecodeDecoder &) = delete;
        BytecodeDecoder(BytecodeDecoder &&) = delete;
        BytecodeDecoder &operator=(BytecodeDecoder &&) = delete;

        const std::vector<BytecodeBlock> &blocks() const { return blocks_; }
        BytecodeBlockId entry_block_id() const { return 0; }

        const std::vector<BytecodeBlockId> &exception_handler_block_ids() const
        {
            return exception_handler_block_ids_;
        }

    private:
        friend class BytecodeInstructionIterator;

        void build_blocks();
        BytecodeInstruction decode_instruction_at(uint32_t pc_offset) const;

        const CodeObject &code_object_;
        InlineCacheTables inline_caches_;
        std::vector<BytecodeBlock> blocks_;
        std::vector<BytecodeBlockId> exception_handler_block_ids_;
    };

}  // namespace cl

#endif  // CL_BYTECODE_DECODER_H
