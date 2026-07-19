#ifndef CL_BYTECODE_INSTRUCTION_H
#define CL_BYTECODE_INSTRUCTION_H

#include "bytecode/bytecode.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace cl
{
    class CodeObject;

    struct BytecodeOperand
    {
        BytecodeOperandKind kind;
        uint32_t value;

        int32_t signed_value() const { return int32_t(value); }
    };

    enum class BytecodeValueLocationKind : uint8_t
    {
        Accumulator,
        Parameter,
        Local,
        Temporary,
    };

    struct BytecodeValueLocation
    {
        BytecodeValueLocationKind kind;
        uint32_t register_index;

        static BytecodeValueLocation accumulator()
        {
            return {BytecodeValueLocationKind::Accumulator, 0};
        }

        bool is_accumulator() const
        {
            return kind == BytecodeValueLocationKind::Accumulator;
        }
    };

    enum class InlineCacheKind : uint8_t
    {
        AttributeRead,
        AttributeMutation,
        ModuleGlobalRead,
        ModuleGlobalMutation,
        FunctionCall,
        KeywordCall,
        Operator,
    };

    struct InlineCacheReference
    {
        InlineCacheKind kind;
        uint8_t index;
    };

    class BytecodeInstruction
    {
    public:
        Bytecode encoded_opcode() const { return encoded_opcode_; }
        Bytecode semantic_opcode() const { return semantic_opcode_; }
        uint32_t pc() const { return pc_; }
        uint32_t next_pc() const { return next_pc_; }
        std::optional<uint32_t> continuation_pc() const
        {
            return continuation_pc_;
        }

        BytecodeControlFlow control_flow() const
        {
            return bytecode_info(encoded_opcode_).control_flow;
        }

        const std::vector<BytecodeOperand> &operands() const
        {
            return operands_;
        }

        const std::vector<BytecodeValueLocation> &sources() const
        {
            return sources_;
        }

        const std::vector<BytecodeValueLocation> &destinations() const
        {
            return destinations_;
        }

        std::optional<InlineCacheReference> cache() const { return cache_; }

        std::optional<InlineCacheReference> cache2() const { return cache2_; }

    private:
        friend BytecodeInstruction decode_instruction(const CodeObject &,
                                                      uint32_t);

        class EffectsBuilder;
        static void decode_value_effects(const CodeObject &,
                                         BytecodeInstruction &);

        Bytecode encoded_opcode_ = Bytecode::Invalid;
        Bytecode semantic_opcode_ = Bytecode::Invalid;
        uint32_t pc_ = 0;
        uint32_t next_pc_ = 0;
        std::optional<uint32_t> continuation_pc_;

        std::vector<BytecodeOperand> operands_;
        std::vector<BytecodeValueLocation> sources_;
        std::vector<BytecodeValueLocation> destinations_;
        std::optional<InlineCacheReference> cache_;
        std::optional<InlineCacheReference> cache2_;
    };

    BytecodeInstruction decode_instruction(const CodeObject &code_object,
                                           uint32_t pc);

}  // namespace cl

#endif  // CL_BYTECODE_INSTRUCTION_H
