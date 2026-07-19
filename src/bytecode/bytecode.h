#ifndef CL_BYTECODE_H
#define CL_BYTECODE_H

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>

namespace cl
{

    inline constexpr int32_t n_fastpath_ldar_star = 16;

    enum class RuntimeIntrinsic0 : uint8_t
    {
        Globals,
        Locals,
        ImportStar,
    };

    enum class BytecodeOperandKind : uint8_t
    {
        Register,
        Constant,
        Smi8,
        Count,
        ArgumentCount,
        KeywordCount,
        ImportLevel,
        RelativeJumpI16,
        AttributeReadCache,
        AttributeMutationCache,
        ModuleGlobalReadCache,
        ModuleGlobalMutationCache,
        FunctionCallCache,
        KeywordCallCache,
        OperatorCache,
        NativeTarget,
        RuntimeIntrinsic,
    };

    enum class BytecodeFormat : uint8_t
    {
        NoOperands,
        Constant,
        Smi8,
        Register,
        RegisterRegister,
        ConstantRegister,
        RegisterCount,
        ConstantModuleGlobalReadCache,
        ConstantModuleGlobalMutationCache,
        RegisterConstantAttributeReadCache,
        RegisterConstantAttributeMutationCache,
        RegisterOperatorCache,
        RegisterRegisterOperatorCache,
        RegisterConstantAttributeReadCacheFunctionCallCacheArgCount,
        MethodKeywordCall,
        SpecialMethodCall,
        Smi8OperatorCache,
        OperatorCache,
        ThreeRegisters,
        FourRegisters,
        FiveRegisters,
        SixRegisters,
        PositionalCall,
        KeywordCall,
        NativeTarget,
        RuntimeIntrinsic,
        CodeObjectCall,
        ImportName,
        RegisterRelativeJump,
        RelativeJump,
        Smi8RelativeJump,
        Invalid,
    };

    enum class BytecodeControlFlow : uint8_t
    {
        Fallthrough,
        UnconditionalJump,
        ConditionalJump,
        Terminator,
        Invalid,
    };

    enum class BytecodeCompoundRole : uint8_t
    {
        None,
        BinaryOperator,
        TernaryOperator,
        BinaryOperatorContinuation,
        TernaryOperatorContinuation,
    };

    enum class Bytecode : uint8_t
    {
#define CL_BYTECODE(name, format, control_flow, compound_role) name,
#include "bytecode/bytecode.def"
#undef CL_BYTECODE
    };

    static constexpr size_t BytecodeTableSize = size_t(Bytecode::Invalid) + 1;
    static constexpr size_t BytecodeOperandLimit = 8;

    struct BytecodeFormatInfo
    {
        std::array<BytecodeOperandKind, BytecodeOperandLimit> operands{};
        uint8_t operand_count = 0;
        bool valid = false;

        constexpr uint8_t operand_width(size_t idx) const
        {
            assert(idx < operand_count);
            return operands[idx] == BytecodeOperandKind::RelativeJumpI16 ? 2
                                                                         : 1;
        }

        constexpr uint8_t operand_offset(size_t idx) const
        {
            assert(idx < operand_count);
            uint8_t offset = 1;
            for(size_t operand_idx = 0; operand_idx < idx; ++operand_idx)
            {
                offset += operand_width(operand_idx);
            }
            return offset;
        }

        constexpr uint8_t length() const
        {
            if(!valid)
            {
                return 0;
            }

            uint8_t result = 1;
            for(size_t idx = 0; idx < operand_count; ++idx)
            {
                result += operand_width(idx);
            }
            return result;
        }

        constexpr int8_t relative_jump_operand_index() const
        {
            for(size_t idx = 0; idx < operand_count; ++idx)
            {
                if(operands[idx] == BytecodeOperandKind::RelativeJumpI16)
                {
                    return int8_t(idx);
                }
            }
            return -1;
        }

        constexpr int8_t relative_jump_operand_offset() const
        {
            int8_t idx = relative_jump_operand_index();
            return idx < 0 ? -1 : int8_t(operand_offset(size_t(idx)));
        }
    };

    template <typename... Operands>
    constexpr BytecodeFormatInfo make_bytecode_format_info(Operands... operands)
    {
        static_assert(sizeof...(operands) <= BytecodeOperandLimit);
        return {{{operands...}}, uint8_t(sizeof...(operands)), true};
    }

    constexpr BytecodeFormatInfo bytecode_format_info(BytecodeFormat format)
    {
        using Operand = BytecodeOperandKind;
        switch(format)
        {
            case BytecodeFormat::NoOperands:
                return make_bytecode_format_info();
            case BytecodeFormat::Constant:
                return make_bytecode_format_info(Operand::Constant);
            case BytecodeFormat::Smi8:
                return make_bytecode_format_info(Operand::Smi8);
            case BytecodeFormat::Register:
                return make_bytecode_format_info(Operand::Register);
            case BytecodeFormat::RegisterRegister:
                return make_bytecode_format_info(Operand::Register,
                                                 Operand::Register);
            case BytecodeFormat::ConstantRegister:
                return make_bytecode_format_info(Operand::Constant,
                                                 Operand::Register);
            case BytecodeFormat::RegisterCount:
                return make_bytecode_format_info(Operand::Register,
                                                 Operand::Count);
            case BytecodeFormat::ConstantModuleGlobalReadCache:
                return make_bytecode_format_info(
                    Operand::Constant, Operand::ModuleGlobalReadCache);
            case BytecodeFormat::ConstantModuleGlobalMutationCache:
                return make_bytecode_format_info(
                    Operand::Constant, Operand::ModuleGlobalMutationCache);
            case BytecodeFormat::RegisterConstantAttributeReadCache:
                return make_bytecode_format_info(Operand::Register,
                                                 Operand::Constant,
                                                 Operand::AttributeReadCache);
            case BytecodeFormat::RegisterConstantAttributeMutationCache:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Constant,
                    Operand::AttributeMutationCache);
            case BytecodeFormat::RegisterOperatorCache:
                return make_bytecode_format_info(Operand::Register,
                                                 Operand::OperatorCache);
            case BytecodeFormat::RegisterRegisterOperatorCache:
                return make_bytecode_format_info(Operand::Register,
                                                 Operand::Register,
                                                 Operand::OperatorCache);
            case BytecodeFormat::
                RegisterConstantAttributeReadCacheFunctionCallCacheArgCount:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Constant,
                    Operand::AttributeReadCache, Operand::FunctionCallCache,
                    Operand::ArgumentCount);
            case BytecodeFormat::MethodKeywordCall:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Constant,
                    Operand::AttributeReadCache, Operand::KeywordCallCache,
                    Operand::ArgumentCount, Operand::Register,
                    Operand::KeywordCount, Operand::Constant);
            case BytecodeFormat::SpecialMethodCall:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Constant,
                    Operand::OperatorCache, Operand::Constant,
                    Operand::Constant);
            case BytecodeFormat::Smi8OperatorCache:
                return make_bytecode_format_info(Operand::Smi8,
                                                 Operand::OperatorCache);
            case BytecodeFormat::OperatorCache:
                return make_bytecode_format_info(Operand::OperatorCache);
            case BytecodeFormat::ThreeRegisters:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Register, Operand::Register);
            case BytecodeFormat::FourRegisters:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Register, Operand::Register,
                    Operand::Register);
            case BytecodeFormat::FiveRegisters:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Register, Operand::Register,
                    Operand::Register, Operand::Register);
            case BytecodeFormat::SixRegisters:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Register, Operand::Register,
                    Operand::Register, Operand::Register, Operand::Register);
            case BytecodeFormat::PositionalCall:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Register,
                    Operand::ArgumentCount, Operand::FunctionCallCache);
            case BytecodeFormat::KeywordCall:
                return make_bytecode_format_info(
                    Operand::Register, Operand::Register,
                    Operand::ArgumentCount, Operand::Register,
                    Operand::KeywordCount, Operand::Constant,
                    Operand::KeywordCallCache);
            case BytecodeFormat::NativeTarget:
                return make_bytecode_format_info(Operand::NativeTarget);
            case BytecodeFormat::RuntimeIntrinsic:
                return make_bytecode_format_info(Operand::RuntimeIntrinsic);
            case BytecodeFormat::CodeObjectCall:
                return make_bytecode_format_info(Operand::Constant,
                                                 Operand::Register,
                                                 Operand::ArgumentCount);
            case BytecodeFormat::ImportName:
                return make_bytecode_format_info(Operand::Constant,
                                                 Operand::ImportLevel);
            case BytecodeFormat::RegisterRelativeJump:
                return make_bytecode_format_info(Operand::Register,
                                                 Operand::RelativeJumpI16);
            case BytecodeFormat::RelativeJump:
                return make_bytecode_format_info(Operand::RelativeJumpI16);
            case BytecodeFormat::Smi8RelativeJump:
                return make_bytecode_format_info(Operand::Smi8,
                                                 Operand::RelativeJumpI16);
            case BytecodeFormat::Invalid:
                return BytecodeFormatInfo{};
        }
        return BytecodeFormatInfo{};
    }

    struct BytecodeInfo
    {
        const char *name;
        BytecodeFormat format;
        BytecodeControlFlow control_flow;
        BytecodeCompoundRole compound_role;

        constexpr BytecodeFormatInfo format_info() const
        {
            return bytecode_format_info(format);
        }

        constexpr uint8_t length() const { return format_info().length(); }

        constexpr int8_t relative_jump_operand_offset() const
        {
            return format_info().relative_jump_operand_offset();
        }
    };

    inline constexpr std::array<BytecodeInfo, BytecodeTableSize>
        BytecodeInfoTable{{
#define CL_BYTECODE(name, format, control_flow, compound_role)                 \
    {#name, BytecodeFormat::format, BytecodeControlFlow::control_flow,         \
     BytecodeCompoundRole::compound_role},
#include "bytecode/bytecode.def"
#undef CL_BYTECODE
        }};

    constexpr const BytecodeInfo &bytecode_info(Bytecode bytecode)
    {
        return BytecodeInfoTable[size_t(bytecode)];
    }

    constexpr uint8_t bytecode_length(Bytecode bytecode)
    {
        return bytecode_info(bytecode).length();
    }

    constexpr bool is_valid_bytecode(Bytecode bytecode)
    {
        return size_t(bytecode) < size_t(Bytecode::Invalid);
    }

    inline const char *bytecode_name(Bytecode bytecode)
    {
        size_t idx = size_t(bytecode);
        return idx < BytecodeInfoTable.size() ? BytecodeInfoTable[idx].name
                                              : "<unknown>";
    }

    static_assert(BytecodeTableSize <= 256);
    static_assert(size_t(Bytecode::Ldar15) - size_t(Bytecode::Ldar0) + 1 ==
                  size_t(n_fastpath_ldar_star));
    static_assert(size_t(Bytecode::Star15) - size_t(Bytecode::Star0) + 1 ==
                  size_t(n_fastpath_ldar_star));
    static_assert(size_t(Bytecode::Star0) == size_t(Bytecode::Ldar15) + 1);

}  // namespace cl

#endif  // CL_BYTECODE_H
