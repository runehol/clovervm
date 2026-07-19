#include "bytecode/bytecode_instruction.h"
#include "bytecode/code_object.h"
#include <cassert>

namespace cl
{
    template <typename Cache>
    static const Cache *cache_at(const std::vector<Cache> &caches,
                                 int16_t index)
    {
        if(index < 0)
        {
            return nullptr;
        }
        assert(size_t(index) < caches.size());
        return &caches[size_t(index)];
    }

    const AttributeReadInlineCache *
    BytecodeInstruction::attribute_read_cache() const
    {
        return inline_cache_tables_ == nullptr
                   ? nullptr
                   : cache_at(inline_cache_tables_->attribute_read_caches,
                              attribute_read_cache_index_);
    }

    const AttributeMutationInlineCache *
    BytecodeInstruction::attribute_mutation_cache() const
    {
        return inline_cache_tables_ == nullptr
                   ? nullptr
                   : cache_at(inline_cache_tables_->attribute_mutation_caches,
                              attribute_mutation_cache_index_);
    }

    const ModuleGlobalReadInlineCache *
    BytecodeInstruction::module_global_read_cache() const
    {
        return inline_cache_tables_ == nullptr
                   ? nullptr
                   : cache_at(inline_cache_tables_->module_global_read_caches,
                              module_global_read_cache_index_);
    }

    const ModuleGlobalMutationInlineCache *
    BytecodeInstruction::module_global_mutation_cache() const
    {
        return inline_cache_tables_ == nullptr
                   ? nullptr
                   : cache_at(
                         inline_cache_tables_->module_global_mutation_caches,
                         module_global_mutation_cache_index_);
    }

    const FunctionCallInlineCache *
    BytecodeInstruction::function_call_cache() const
    {
        return inline_cache_tables_ == nullptr
                   ? nullptr
                   : cache_at(inline_cache_tables_->function_call_caches,
                              function_call_cache_index_);
    }

    const KeywordCallInlineCache *
    BytecodeInstruction::keyword_call_cache() const
    {
        return inline_cache_tables_ == nullptr
                   ? nullptr
                   : cache_at(inline_cache_tables_->keyword_call_caches,
                              keyword_call_cache_index_);
    }

    const OperatorInlineCache *BytecodeInstruction::operator_cache() const
    {
        return inline_cache_tables_ == nullptr
                   ? nullptr
                   : cache_at(inline_cache_tables_->operator_caches,
                              operator_cache_index_);
    }

    static int16_t read_int16_le(const uint8_t *p)
    {
        uint16_t raw = uint16_t(p[0]) | (uint16_t(p[1]) << 8);
        return int16_t(raw);
    }

    static BytecodeValueLocation
    register_location(const CodeObject &code_object, uint32_t register_index)
    {
        uint32_t n_parameters = code_object.function_signature.n_parameters;
        if(register_index < n_parameters)
        {
            return {BytecodeValueLocationKind::Parameter, register_index};
        }

        uint32_t first_local =
            code_object.get_padded_n_parameters() + FrameHeaderSize;
        uint32_t first_temporary = first_local + code_object.n_locals;
        if(register_index < first_temporary)
        {
            assert(register_index >= first_local);
            return {BytecodeValueLocationKind::Local, register_index};
        }
        return {BytecodeValueLocationKind::Temporary, register_index};
    }

    class BytecodeInstruction::EffectsBuilder
    {
    public:
        EffectsBuilder(const CodeObject &_code_object,
                       BytecodeInstruction &_instruction)
            : code_object(_code_object), instruction(_instruction)
        {
        }

        uint32_t operand(size_t idx) const
        {
            assert(idx < instruction.operands_.size());
            return instruction.operands_[idx].value;
        }

        void source_accumulator()
        {
            instruction.sources_.push_back(
                BytecodeValueLocation::accumulator());
        }

        void destination_accumulator()
        {
            instruction.destinations_.push_back(
                BytecodeValueLocation::accumulator());
        }

        void source_register(size_t operand_idx, uint32_t count = 1)
        {
            add_registers(instruction.sources_, operand(operand_idx), count);
        }

        void destination_register(size_t operand_idx, uint32_t count = 1)
        {
            add_registers(instruction.destinations_, operand(operand_idx),
                          count);
        }

        void source_parameters(uint32_t count)
        {
            add_registers(instruction.sources_, 0, count);
        }

        void source_locals(uint32_t count)
        {
            uint32_t first_local =
                code_object.get_padded_n_parameters() + FrameHeaderSize;
            add_registers(instruction.sources_, first_local, count);
        }

    private:
        void add_registers(std::vector<BytecodeValueLocation> &locations,
                           uint32_t first_register, uint32_t count)
        {
            for(uint32_t offset = 0; offset < count; ++offset)
            {
                locations.push_back(
                    register_location(code_object, first_register + offset));
            }
        }

        const CodeObject &code_object;
        BytecodeInstruction &instruction;
    };

    static uint32_t call_arity(Bytecode opcode, Bytecode first_opcode)
    {
        return uint32_t(opcode) - uint32_t(first_opcode);
    }

    void
    BytecodeInstruction::decode_value_effects(const CodeObject &code_object,
                                              BytecodeInstruction &instruction)
    {
        EffectsBuilder effects(code_object, instruction);
        Bytecode opcode = instruction.semantic_opcode();

        if(opcode >= Bytecode::CallIntrinsic0 &&
           opcode <= Bytecode::CallIntrinsic7)
        {
            effects.source_parameters(
                call_arity(opcode, Bytecode::CallIntrinsic0));
            effects.destination_accumulator();
            return;
        }
        if(opcode >= Bytecode::CallExtension0 &&
           opcode <= Bytecode::CallExtension7)
        {
            effects.source_parameters(
                call_arity(opcode, Bytecode::CallExtension0));
            effects.destination_accumulator();
            return;
        }
        if(opcode >= Bytecode::CallSpecialMethod0 &&
           opcode <= Bytecode::CallSpecialMethod3)
        {
            effects.source_register(
                0, call_arity(opcode, Bytecode::CallSpecialMethod0) + 1);
            effects.destination_accumulator();
            return;
        }

        switch(opcode)
        {
            case Bytecode::LdaConstant:
            case Bytecode::LdaSmi:
            case Bytecode::LdaTrue:
            case Bytecode::LdaFalse:
            case Bytecode::LdaNone:
            case Bytecode::LdaGlobal:
            case Bytecode::LdaActiveException:
            case Bytecode::CreateFunction:
            case Bytecode::CreateInstanceKnownClass:
                effects.destination_accumulator();
                break;

            case Bytecode::BuildClass:
                effects.source_parameters(
                    code_object.function_signature.n_parameters);
                effects.source_locals(code_object.n_locals);
                effects.destination_accumulator();
                break;

            case Bytecode::Ldar:
            case Bytecode::LoadLocalChecked:
                effects.source_register(0);
                effects.destination_accumulator();
                break;

            case Bytecode::ClearLocal:
                effects.destination_register(0);
                break;

            case Bytecode::Star:
                effects.source_accumulator();
                effects.destination_register(0);
                break;

            case Bytecode::Mov:
                effects.source_register(1);
                effects.destination_register(0);
                break;

            case Bytecode::StaGlobal:
                effects.source_accumulator();
                break;

            case Bytecode::DelLocal:
                effects.source_register(0);
                effects.destination_register(0);
                break;

            case Bytecode::LoadAttr:
                effects.source_register(0);
                effects.destination_accumulator();
                break;

            case Bytecode::StoreAttr:
                effects.source_register(0);
                effects.source_accumulator();
                break;

            case Bytecode::DelAttr:
                effects.source_register(0);
                break;

            case Bytecode::GetItem:
            case Bytecode::DelItem:
                effects.source_register(0);
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::SetItem:
                effects.source_register(0);
                effects.source_register(1);
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::CallMethodAttrPositional:
                effects.source_register(0, effects.operand(4) + 1);
                effects.destination_accumulator();
                break;

            case Bytecode::CallMethodAttrKeyword:
                effects.source_register(0, effects.operand(4) + 1);
                if(effects.operand(6) != 0)
                {
                    effects.source_register(5, effects.operand(6));
                }
                effects.destination_accumulator();
                break;

            case Bytecode::Add:
            case Bytecode::Sub:
            case Bytecode::Mul:
            case Bytecode::MatMul:
            case Bytecode::TrueDiv:
            case Bytecode::FloorDiv:
            case Bytecode::BinaryPow:
            case Bytecode::LShift:
            case Bytecode::RShift:
            case Bytecode::Mod:
            case Bytecode::Or:
            case Bytecode::And:
            case Bytecode::Xor:
            case Bytecode::TestEqual:
            case Bytecode::TestNotEqual:
            case Bytecode::TestLess:
            case Bytecode::TestLessEqual:
            case Bytecode::TestGreater:
            case Bytecode::TestGreaterEqual:
            case Bytecode::TestIs:
            case Bytecode::TestIsNot:
            case Bytecode::Contains:
                effects.source_register(0);
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::TernaryPow:
                effects.source_register(0);
                effects.source_register(1);
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::AddSmi:
            case Bytecode::SubSmi:
            case Bytecode::MulSmi:
            case Bytecode::FloorDivSmi:
            case Bytecode::BinaryPowSmi:
            case Bytecode::LShiftSmi:
            case Bytecode::RShiftSmi:
            case Bytecode::ModSmi:
            case Bytecode::OrSmi:
            case Bytecode::AndSmi:
            case Bytecode::XorSmi:
            case Bytecode::Not:
            case Bytecode::ToBool:
            case Bytecode::ToBoolNot:
            case Bytecode::Neg:
            case Bytecode::Pos:
            case Bytecode::Sqrt:
            case Bytecode::Invert:
            case Bytecode::CanonicalizeHash:
            case Bytecode::IsInstanceOfKnownClass:
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::CheckOperatorNotImplemented:
            case Bytecode::CheckTernaryOperatorNotImplemented:
            case Bytecode::CheckInitReturnedNone:
                effects.source_accumulator();
                break;

            case Bytecode::DictPromoteStringKeyed:
            case Bytecode::DictResizeForInsert:
                effects.source_register(0);
                break;

            case Bytecode::DictTryStringKeyedSetDefault:
                effects.source_register(0);
                effects.source_register(1);
                effects.source_register(2);
                effects.destination_register(3);
                effects.destination_accumulator();
                break;

            case Bytecode::DictTryStringKeyedPop:
                effects.source_register(0);
                effects.source_register(1);
                effects.destination_register(2);
                effects.destination_accumulator();
                break;

            case Bytecode::DictProbeStart:
                effects.source_register(0);
                effects.source_accumulator();
                effects.destination_register(1);
                effects.destination_register(2);
                break;

            case Bytecode::DictProbeForLookup:
            case Bytecode::DictProbeForInsert:
                effects.source_register(0);
                effects.source_register(1);
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::DictProbeAdvance:
            case Bytecode::DictEntryKey:
            case Bytecode::DictEntryValue:
                effects.source_register(0);
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::DictEntryStillMatches:
                effects.source_register(0);
                effects.source_register(1);
                effects.source_register(2);
                effects.source_register(3);
                effects.source_register(4);
                effects.destination_accumulator();
                break;

            case Bytecode::DictInsertNew:
                effects.source_register(0);
                effects.source_register(1);
                effects.source_register(2);
                effects.source_register(3);
                effects.source_register(4);
                effects.source_register(5);
                effects.destination_accumulator();
                break;

            case Bytecode::DictOverwriteEntry:
                effects.source_register(0);
                effects.source_register(1);
                effects.source_register(2);
                effects.destination_accumulator();
                break;

            case Bytecode::DictDeleteEntry:
                effects.source_register(0);
                effects.source_register(1);
                effects.destination_accumulator();
                break;

            case Bytecode::CallPositional:
                effects.source_register(0);
                if(effects.operand(2) != 0)
                {
                    effects.source_register(1, effects.operand(2));
                }
                effects.destination_accumulator();
                break;

            case Bytecode::CallKeyword:
                effects.source_register(0);
                if(effects.operand(2) != 0)
                {
                    effects.source_register(1, effects.operand(2));
                }
                if(effects.operand(4) != 0)
                {
                    effects.source_register(3, effects.operand(4));
                }
                effects.destination_accumulator();
                break;

            case Bytecode::CallRuntimeIntrinsic0:
                if(RuntimeIntrinsic0(effects.operand(0)) ==
                   RuntimeIntrinsic0::ImportStar)
                {
                    effects.source_accumulator();
                }
                effects.destination_accumulator();
                break;

            case Bytecode::CallCodeObject:
                if(effects.operand(2) != 0)
                {
                    effects.source_register(1, effects.operand(2));
                }
                effects.destination_accumulator();
                break;

            case Bytecode::ImportName:
            case Bytecode::ImportFrom:
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::ForIter:
                effects.source_register(0);
                effects.destination_accumulator();
                break;

            case Bytecode::ForPrepRange1:
                effects.source_register(0, 2);
                effects.destination_register(0, 2);
                break;
            case Bytecode::ForPrepRange2:
                effects.source_register(0, 3);
                effects.destination_register(0, 3);
                break;
            case Bytecode::ForPrepRange3:
                effects.source_register(0, 4);
                effects.destination_register(0, 4);
                break;

            case Bytecode::ForIterRange1:
                effects.source_register(0, 2);
                effects.destination_register(0);
                effects.destination_accumulator();
                break;
            case Bytecode::ForIterRangeStep:
                effects.source_register(0, 3);
                effects.destination_register(0);
                effects.destination_accumulator();
                break;

            case Bytecode::CreateDict:
                if(effects.operand(1) != 0)
                {
                    effects.source_register(0, effects.operand(1) * 2);
                }
                effects.destination_accumulator();
                break;
            case Bytecode::CreateList:
            case Bytecode::CreateTuple:
                if(effects.operand(1) != 0)
                {
                    effects.source_register(0, effects.operand(1));
                }
                effects.destination_accumulator();
                break;

            case Bytecode::CreateBinarySlice:
                effects.source_register(0);
                effects.source_accumulator();
                effects.destination_accumulator();
                break;
            case Bytecode::CreateTernarySlice:
                effects.source_register(0);
                effects.source_register(1);
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::CreateFunctionWithDefaults:
                effects.source_register(1);
                effects.destination_accumulator();
                break;

            case Bytecode::CreateClass:
                effects.source_register(1, ClassBodyParameterCount);
                effects.destination_accumulator();
                break;

            case Bytecode::WriteStdout:
                effects.source_accumulator();
                effects.destination_accumulator();
                break;

            case Bytecode::RaiseAssertionErrorWithMessage:
            case Bytecode::RaiseUnwind:
                effects.source_accumulator();
                break;
            case Bytecode::RaiseUnwindWithContext:
                effects.source_accumulator();
                effects.source_register(0);
                break;

            case Bytecode::JumpIfTrue:
            case Bytecode::JumpIfFalse:
            case Bytecode::JumpIfEqualSmi:
            case Bytecode::Return:
            case Bytecode::ReturnOrRaiseException:
            case Bytecode::ReturnToNative:
            case Bytecode::ActiveExceptionIsInstance:
                effects.source_accumulator();
                if(opcode == Bytecode::ActiveExceptionIsInstance)
                {
                    effects.destination_accumulator();
                }
                break;

            case Bytecode::DrainActiveExceptionInto:
                effects.destination_register(0);
                break;

            case Bytecode::DelGlobal:
            case Bytecode::Nop:
            case Bytecode::RaiseAssertionError:
            case Bytecode::RaiseBare:
            case Bytecode::ClearActiveException:
            case Bytecode::ReturnExceptionMarkerToNative:
            case Bytecode::ReraiseActiveException:
            case Bytecode::Jump:
                break;

            case Bytecode::Ldar0:
            case Bytecode::Ldar1:
            case Bytecode::Ldar2:
            case Bytecode::Ldar3:
            case Bytecode::Ldar4:
            case Bytecode::Ldar5:
            case Bytecode::Ldar6:
            case Bytecode::Ldar7:
            case Bytecode::Ldar8:
            case Bytecode::Ldar9:
            case Bytecode::Ldar10:
            case Bytecode::Ldar11:
            case Bytecode::Ldar12:
            case Bytecode::Ldar13:
            case Bytecode::Ldar14:
            case Bytecode::Ldar15:
            case Bytecode::Star0:
            case Bytecode::Star1:
            case Bytecode::Star2:
            case Bytecode::Star3:
            case Bytecode::Star4:
            case Bytecode::Star5:
            case Bytecode::Star6:
            case Bytecode::Star7:
            case Bytecode::Star8:
            case Bytecode::Star9:
            case Bytecode::Star10:
            case Bytecode::Star11:
            case Bytecode::Star12:
            case Bytecode::Star13:
            case Bytecode::Star14:
            case Bytecode::Star15:
            case Bytecode::CallSpecialMethod0:
            case Bytecode::CallSpecialMethod1:
            case Bytecode::CallSpecialMethod2:
            case Bytecode::CallSpecialMethod3:
            case Bytecode::CallIntrinsic0:
            case Bytecode::CallIntrinsic1:
            case Bytecode::CallIntrinsic2:
            case Bytecode::CallIntrinsic3:
            case Bytecode::CallIntrinsic4:
            case Bytecode::CallIntrinsic5:
            case Bytecode::CallIntrinsic6:
            case Bytecode::CallIntrinsic7:
            case Bytecode::CallExtension0:
            case Bytecode::CallExtension1:
            case Bytecode::CallExtension2:
            case Bytecode::CallExtension3:
            case Bytecode::CallExtension4:
            case Bytecode::CallExtension5:
            case Bytecode::CallExtension6:
            case Bytecode::CallExtension7:
            case Bytecode::Invalid:
                assert(false && "unreachable semantic opcode");
                break;
        }
    }

    BytecodeInstruction decode_instruction(const CodeObject &code_object,
                                           uint32_t pc_offset)
    {
        assert(pc_offset < code_object.size());
        BytecodeInstruction instruction;
        instruction.pc_offset_ = pc_offset;
        instruction.encoded_opcode_ = Bytecode(code_object.code[pc_offset]);
        assert(is_valid_bytecode(instruction.encoded_opcode_));
        instruction.semantic_opcode_ = instruction.encoded_opcode_;

        const BytecodeInfo &info = bytecode_info(instruction.encoded_opcode_);
        BytecodeFormatInfo format = info.format_info();
        uint32_t physical_next_pc_offset = pc_offset + format.length();
        assert(physical_next_pc_offset <= code_object.size());

        for(size_t idx = 0; idx < format.operand_count; ++idx)
        {
            BytecodeOperandKind kind = format.operands[idx];
            uint32_t operand_offset = pc_offset + format.operand_offset(idx);
            uint32_t value;
            if(kind == BytecodeOperandKind::Register)
            {
                value = code_object.decode_reg(
                    int8_t(code_object.code[operand_offset]));
            }
            else if(kind == BytecodeOperandKind::Smi8)
            {
                value =
                    uint32_t(int32_t(int8_t(code_object.code[operand_offset])));
            }
            else if(kind == BytecodeOperandKind::RelativeJumpI16)
            {
                int32_t relative_offset =
                    read_int16_le(&code_object.code[operand_offset]);
                value = uint32_t(relative_offset);
                assert(!instruction.jump_target_pc_offset_.has_value());
                instruction.jump_target_pc_offset_ =
                    uint32_t(int32_t(operand_offset + 2) + relative_offset);
            }
            else
            {
                value = code_object.code[operand_offset];
            }

            instruction.operands_.push_back({kind, value});
            switch(kind)
            {
                case BytecodeOperandKind::AttributeReadCache:
                    assert(instruction.attribute_read_cache_index_ == -1);
                    instruction.attribute_read_cache_index_ = int16_t(value);
                    break;
                case BytecodeOperandKind::AttributeMutationCache:
                    assert(instruction.attribute_mutation_cache_index_ == -1);
                    instruction.attribute_mutation_cache_index_ =
                        int16_t(value);
                    break;
                case BytecodeOperandKind::ModuleGlobalReadCache:
                    assert(instruction.module_global_read_cache_index_ == -1);
                    instruction.module_global_read_cache_index_ =
                        int16_t(value);
                    break;
                case BytecodeOperandKind::ModuleGlobalMutationCache:
                    assert(instruction.module_global_mutation_cache_index_ ==
                           -1);
                    instruction.module_global_mutation_cache_index_ =
                        int16_t(value);
                    break;
                case BytecodeOperandKind::FunctionCallCache:
                    assert(instruction.function_call_cache_index_ == -1);
                    instruction.function_call_cache_index_ = int16_t(value);
                    break;
                case BytecodeOperandKind::KeywordCallCache:
                    assert(instruction.keyword_call_cache_index_ == -1);
                    instruction.keyword_call_cache_index_ = int16_t(value);
                    break;
                case BytecodeOperandKind::OperatorCache:
                    assert(instruction.operator_cache_index_ == -1);
                    instruction.operator_cache_index_ = int16_t(value);
                    break;
                default:
                    break;
            }
        }

        if(instruction.encoded_opcode_ >= Bytecode::Ldar0 &&
           instruction.encoded_opcode_ <= Bytecode::Ldar15)
        {
            uint32_t fast_index = uint32_t(instruction.encoded_opcode_) -
                                  uint32_t(Bytecode::Ldar0);
            instruction.semantic_opcode_ = Bytecode::Ldar;
            instruction.operands_.push_back(
                {BytecodeOperandKind::Register,
                 code_object.decode_reg(-int8_t(fast_index) -
                                        FrameHeaderSizeBelowFp - 1)});
        }
        else if(instruction.encoded_opcode_ >= Bytecode::Star0 &&
                instruction.encoded_opcode_ <= Bytecode::Star15)
        {
            uint32_t fast_index = uint32_t(instruction.encoded_opcode_) -
                                  uint32_t(Bytecode::Star0);
            instruction.semantic_opcode_ = Bytecode::Star;
            instruction.operands_.push_back(
                {BytecodeOperandKind::Register,
                 code_object.decode_reg(-int8_t(fast_index) -
                                        FrameHeaderSizeBelowFp - 1)});
        }

        instruction.next_pc_offset_ = physical_next_pc_offset;
        if(info.compound_role == BytecodeCompoundRole::BinaryOperator ||
           info.compound_role == BytecodeCompoundRole::TernaryOperator)
        {
            Bytecode expected_continuation =
                info.compound_role == BytecodeCompoundRole::BinaryOperator
                    ? Bytecode::CheckOperatorNotImplemented
                    : Bytecode::CheckTernaryOperatorNotImplemented;
            assert(physical_next_pc_offset < code_object.size());
            assert(Bytecode(code_object.code[physical_next_pc_offset]) ==
                   expected_continuation);
            instruction.continuation_pc_offset_ = physical_next_pc_offset;
            instruction.next_pc_offset_ =
                physical_next_pc_offset +
                bytecode_length(expected_continuation);
        }

        BytecodeInstruction::decode_value_effects(code_object, instruction);
        return instruction;
    }

}  // namespace cl
