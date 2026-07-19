#ifndef CL_BYTECODE_INSTRUCTION_H
#define CL_BYTECODE_INSTRUCTION_H

#include "bytecode/bytecode.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace cl
{
    class AttributeMutationInlineCache;
    class AttributeReadInlineCache;
    class BytecodeDecoder;
    class CodeObject;
    class ModuleGlobalMutationInlineCache;
    class ModuleGlobalReadInlineCache;
    struct FunctionCallInlineCache;
    struct InlineCacheTables;
    struct KeywordCallInlineCache;
    struct OperatorInlineCache;

    struct BytecodeOperand
    {
        BytecodeOperandKind kind;
        // Signed operand kinds retain their encoded value, sign-extended to
        // int32_t. Resolved control-flow targets are exposed separately.
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

    class BytecodeInstruction
    {
    public:
        // Every offset is relative to the CodeObject passed to
        // decode_instruction(). Interpreter PCs are pointers and are not
        // stored in a decoded instruction.
        Bytecode encoded_opcode() const { return encoded_opcode_; }
        Bytecode semantic_opcode() const { return semantic_opcode_; }
        uint32_t pc_offset() const { return pc_offset_; }
        uint32_t next_pc_offset() const { return next_pc_offset_; }
        std::optional<uint32_t> continuation_pc_offset() const
        {
            return continuation_pc_offset_;
        }
        std::optional<uint32_t> jump_target_pc_offset() const
        {
            return jump_target_pc_offset_;
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

        const AttributeReadInlineCache *attribute_read_cache() const;
        const AttributeMutationInlineCache *attribute_mutation_cache() const;
        const ModuleGlobalReadInlineCache *module_global_read_cache() const;
        const ModuleGlobalMutationInlineCache *
        module_global_mutation_cache() const;
        const FunctionCallInlineCache *function_call_cache() const;
        const KeywordCallInlineCache *keyword_call_cache() const;
        const OperatorInlineCache *operator_cache() const;

    private:
        friend class BytecodeDecoder;
        friend BytecodeInstruction decode_instruction(const CodeObject &,
                                                      uint32_t);

        class EffectsBuilder;
        static void decode_value_effects(const CodeObject &,
                                         BytecodeInstruction &);

        Bytecode encoded_opcode_ = Bytecode::Invalid;
        Bytecode semantic_opcode_ = Bytecode::Invalid;
        uint32_t pc_offset_ = 0;
        uint32_t next_pc_offset_ = 0;
        std::optional<uint32_t> continuation_pc_offset_;
        std::optional<uint32_t> jump_target_pc_offset_;

        std::vector<BytecodeOperand> operands_;
        std::vector<BytecodeValueLocation> sources_;
        std::vector<BytecodeValueLocation> destinations_;

        const InlineCacheTables *inline_cache_tables_ = nullptr;
        int16_t attribute_read_cache_index_ = -1;
        int16_t attribute_mutation_cache_index_ = -1;
        int16_t module_global_read_cache_index_ = -1;
        int16_t module_global_mutation_cache_index_ = -1;
        int16_t function_call_cache_index_ = -1;
        int16_t keyword_call_cache_index_ = -1;
        int16_t operator_cache_index_ = -1;
    };

    BytecodeInstruction decode_instruction(const CodeObject &code_object,
                                           uint32_t pc_offset);

}  // namespace cl

#endif  // CL_BYTECODE_INSTRUCTION_H
