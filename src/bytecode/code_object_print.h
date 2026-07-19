#ifndef CL_CODE_OBJECT_PRINT_H
#define CL_CODE_OBJECT_PRINT_H

#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "bytecode/bytecode_instruction.h"
#include "bytecode/code_object.h"
#include "object_model/class_object.h"
#include <cassert>
#include <fmt/format.h>
#include <fmt/xchar.h>

namespace cl
{
    template <typename Out> Out format_string_contents(Out out, String *str)
    {
        for(uint32_t idx = 0; idx < str->count.extract(); ++idx)
        {
            wchar_t ch = str->data[idx];
            switch(ch)
            {
                case L'\\':
                    out = format_to(out, "\\\\");
                    break;
                case L'"':
                    out = format_to(out, "\\\"");
                    break;
                case L'\n':
                    out = format_to(out, "\\n");
                    break;
                case L'\r':
                    out = format_to(out, "\\r");
                    break;
                case L'\t':
                    out = format_to(out, "\\t");
                    break;
                default:
                    if(ch >= 0x20 && ch <= 0x7e)
                    {
                        out = format_to(out, "{}", static_cast<char>(ch));
                    }
                    else
                    {
                        out = format_to(out, "\\u{:04x}",
                                        static_cast<uint32_t>(ch));
                    }
                    break;
            }
        }
        return out;
    }

    template <typename Out> Out format_string_constant(Out out, String *str)
    {
        out = format_to(out, "\"");
        out = format_string_contents(out, str);
        return format_to(out, "\"");
    }

    template <typename Out> Out format_tuple_constant(Out out, Tuple *tuple)
    {
        format_to(out, "(");
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            if(idx > 0)
            {
                format_to(out, ", ");
            }
            Value item = tuple->item_unchecked(idx);
            if(can_convert_to<String>(item))
            {
                out = format_string_constant(out, item.get_ptr<String>());
            }
            else if(item.is_smi())
            {
                format_to(out, "{}", item.get_smi());
            }
            else
            {
                format_to(out, "<object>");
            }
        }
        if(tuple->size() == 1)
        {
            format_to(out, ",");
        }
        return format_to(out, ")");
    }

    template <typename Out> Out format_class_constant(Out out, ClassObject *cls)
    {
        out = format_to(out, "<class ");
        out = format_string_contents(out, cls->get_name().extract());
        return format_to(out, ">");
    }
}  // namespace cl

template <> struct fmt::formatter<cl::RuntimeIntrinsic0>
{
    constexpr auto parse(format_parse_context &ctx) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const cl::RuntimeIntrinsic0 intrinsic, FormatContext &ctx) const
        -> decltype(ctx.out())
    {
        auto &&out = ctx.out();
        switch(intrinsic)
        {
            case cl::RuntimeIntrinsic0::Globals:
                return format_to(out, "Globals");
            case cl::RuntimeIntrinsic0::Locals:
                return format_to(out, "Locals");
            case cl::RuntimeIntrinsic0::ImportStar:
                return format_to(out, "ImportStar");
        }
        return format_to(out, "<unknown>");
    }
};

template <> struct fmt::formatter<cl::Bytecode>
{
    constexpr auto parse(format_parse_context &ctx) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const cl::Bytecode b, FormatContext &ctx) const
        -> decltype(ctx.out())
    {
        return format_to(ctx.out(), "{}", cl::bytecode_name(b));
    }
};
template <> struct fmt::formatter<cl::CodeObject>
{
    constexpr auto parse(format_parse_context &ctx) { return ctx.end(); }

    template <typename Out>
    void print_reg(Out &out, const cl::CodeObject &code_obj,
                   int8_t encoded_reg) const
    {
        if(encoded_reg >= 0)
        {
            uint32_t reg = code_obj.get_padded_n_parameters() - 1 +
                           cl::FrameHeaderSizeAboveFp - encoded_reg;
            format_to(out, "p{}", reg);
        }
        else if(encoded_reg < 0)
        {
            uint32_t frame_slot = -encoded_reg - cl::FrameHeaderSizeBelowFp - 1;
            format_to(out, "r{}", frame_slot);
        }
    }

    template <typename Out>
    void disassemble_reg(const cl::CodeObject &code_obj, Out &out,
                         uint32_t pc) const
    {
        print_reg(out, code_obj, code_obj.code[pc]);
    }

    template <typename Out>
    void print_reg_span(Out &out, const cl::CodeObject &code_obj,
                        int8_t first_reg, uint8_t n_regs) const
    {
        format_to(out, "{{");
        print_reg(out, code_obj, first_reg);
        if(n_regs < 2)
        {
            format_to(out, ":{}", n_regs);
        }
        else
        {
            format_to(out, "..");
            print_reg(out, code_obj,
                      static_cast<int8_t>(first_reg - n_regs + 1));
        }
        format_to(out, "}}");
    }

    template <typename Out>
    void disassemble_reg_span(const cl::CodeObject &code_obj, Out &out,
                              uint32_t pc) const
    {
        print_reg_span(out, code_obj, static_cast<int8_t>(code_obj.code[pc]),
                       code_obj.code[pc + 1]);
    }

    template <typename Out>
    void disassemble_constant(const cl::CodeObject &code_obj, Out &out,
                              uint32_t pc) const
    {
        format_to(out, "c[{}]", code_obj.code[pc]);
    }

    template <typename Out>
    void disassemble_read_cache(const cl::CodeObject &code_obj, Out &out,
                                uint32_t pc) const
    {
        format_to(out, "read_ic[{}]", code_obj.code[pc]);
    }

    template <typename Out>
    void disassemble_mutation_cache(const cl::CodeObject &code_obj, Out &out,
                                    uint32_t pc) const
    {
        format_to(out, "mutation_ic[{}]", code_obj.code[pc]);
    }

    template <typename Out>
    void disassemble_module_global_read_cache(const cl::CodeObject &code_obj,
                                              Out &out, uint32_t pc) const
    {
        format_to(out, "module_global_read_ic[{}]", code_obj.code[pc]);
    }

    template <typename Out>
    void
    disassemble_module_global_mutation_cache(const cl::CodeObject &code_obj,
                                             Out &out, uint32_t pc) const
    {
        format_to(out, "module_global_mutation_ic[{}]", code_obj.code[pc]);
    }

    template <typename Out>
    void disassemble_function_call_cache(const cl::CodeObject &code_obj,
                                         Out &out, uint32_t pc) const
    {
        format_to(out, "call_ic[{}]", code_obj.code[pc]);
    }

    template <typename Out>
    void disassemble_operator_cache(const cl::CodeObject &code_obj, Out &out,
                                    uint32_t pc) const
    {
        format_to(out, "operator_ic[{}]", code_obj.code[pc]);
    }

    template <typename Out>
    void disassemble_smi8(const cl::CodeObject &code_obj, Out &out,
                          uint32_t pc) const
    {
        int32_t smi = int8_t(code_obj.code[pc]);
        format_to(out, "{}", smi);
    }

    static int16_t read_int16_le(const uint8_t *p)
    {
        return (p[0] << 0) | (p[1] << 8);
    }

    template <typename Out>
    void disassemble_jump_target(const cl::CodeObject &code_obj, Out &out,
                                 uint32_t pc) const
    {
        int16_t rel_target = read_int16_le(&code_obj.code[pc]);
        uint32_t actual_target = pc + 2 + rel_target;
        format_to(out, "{}", actual_target);
    }

    template <typename Out>
    uint32_t disassemble_instruction(const cl::CodeObject &code_obj, Out &out,
                                     uint32_t pc_offset) const
    {
        return disassemble_instruction(
            code_obj, out, cl::decode_instruction(code_obj, pc_offset));
    }

    template <typename Out>
    uint32_t
    disassemble_instruction(const cl::CodeObject &code_obj, Out &out,
                            const cl::BytecodeInstruction &instruction) const
    {
        uint32_t pc = instruction.pc_offset();
        cl::Bytecode bc = instruction.encoded_opcode();
        format_to(out, "{:5d} {}", pc, bc);

        ++pc;

        switch(bc)
        {
            case cl::Bytecode::LdaTrue:
            case cl::Bytecode::LdaFalse:
            case cl::Bytecode::LdaNone:
                break;

            case cl::Bytecode::LdaConstant:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                break;

            case cl::Bytecode::LdaSmi:
                format_to(out, " ");
                disassemble_smi8(code_obj, out, pc++);
                break;

            case cl::Bytecode::Ldar:
            case cl::Bytecode::LoadLocalChecked:
            case cl::Bytecode::ClearLocal:
            case cl::Bytecode::Star:
            case cl::Bytecode::DelLocal:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::Mov:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::Ldar0:
            case cl::Bytecode::Ldar1:
            case cl::Bytecode::Ldar2:
            case cl::Bytecode::Ldar3:
            case cl::Bytecode::Ldar4:
            case cl::Bytecode::Ldar5:
            case cl::Bytecode::Ldar6:
            case cl::Bytecode::Ldar7:
            case cl::Bytecode::Ldar8:
            case cl::Bytecode::Ldar9:
            case cl::Bytecode::Ldar10:
            case cl::Bytecode::Ldar11:
            case cl::Bytecode::Ldar12:
            case cl::Bytecode::Ldar13:
            case cl::Bytecode::Ldar14:
            case cl::Bytecode::Ldar15:
            case cl::Bytecode::Star0:
            case cl::Bytecode::Star1:
            case cl::Bytecode::Star2:
            case cl::Bytecode::Star3:
            case cl::Bytecode::Star4:
            case cl::Bytecode::Star5:
            case cl::Bytecode::Star6:
            case cl::Bytecode::Star7:
            case cl::Bytecode::Star8:
            case cl::Bytecode::Star9:
            case cl::Bytecode::Star10:
            case cl::Bytecode::Star11:
            case cl::Bytecode::Star12:
            case cl::Bytecode::Star13:
            case cl::Bytecode::Star14:
            case cl::Bytecode::Star15:
                break;

            case cl::Bytecode::LdaGlobal:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_module_global_read_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::StaGlobal:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_module_global_mutation_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::DelGlobal:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                break;

            case cl::Bytecode::LoadAttr:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_read_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::StoreAttr:
            case cl::Bytecode::DelAttr:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_mutation_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::GetItem:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_operator_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::SetItem:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_operator_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::DelItem:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_operator_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::CallMethodAttrPositional:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_read_cache(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_function_call_cache(code_obj, out, pc++);
                format_to(out, ", ");
                format_to(out, "{}", code_obj.code[pc++]);
                break;

            case cl::Bytecode::CallMethodAttrKeyword:
                {
                    format_to(out, " ");
                    int8_t first_arg_reg = code_obj.code[pc++];
                    uint32_t name_idx = pc++;
                    uint32_t read_cache_idx = pc++;
                    uint32_t call_cache_idx = pc++;
                    uint8_t n_pos_args = code_obj.code[pc++];
                    int8_t first_kw_value_reg = code_obj.code[pc++];
                    uint8_t n_kw_args = code_obj.code[pc++];
                    uint32_t keyword_names_idx = pc++;
                    print_reg(out, code_obj, first_arg_reg);
                    format_to(out, ", ");
                    disassemble_constant(code_obj, out, name_idx);
                    format_to(out, ", ");
                    disassemble_read_cache(code_obj, out, read_cache_idx);
                    format_to(out, ", ");
                    disassemble_function_call_cache(code_obj, out,
                                                    call_cache_idx);
                    format_to(out, ", ");
                    print_reg_span(out, code_obj, first_arg_reg, n_pos_args);
                    format_to(out, ", kw_values=");
                    print_reg_span(out, code_obj, first_kw_value_reg,
                                   n_kw_args);
                    format_to(out, ", kw=");
                    uint8_t constant_idx = code_obj.code[keyword_names_idx];
                    cl::Value constant =
                        code_obj.constant_table[constant_idx].value();
                    if(cl::can_convert_to<cl::Tuple>(constant))
                    {
                        cl::format_tuple_constant(
                            out, constant.get_ptr<cl::Tuple>());
                    }
                    else
                    {
                        disassemble_constant(code_obj, out, keyword_names_idx);
                    }
                }
                break;

            case cl::Bytecode::CallSpecialMethod0:
            case cl::Bytecode::CallSpecialMethod1:
            case cl::Bytecode::CallSpecialMethod2:
            case cl::Bytecode::CallSpecialMethod3:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_operator_cache(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_constant(code_obj, out, pc++);
                break;

            case cl::Bytecode::TestIs:
            case cl::Bytecode::TestIsNot:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::Contains:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_operator_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::Add:
            case cl::Bytecode::Sub:
            case cl::Bytecode::Mul:
            case cl::Bytecode::MatMul:
            case cl::Bytecode::TrueDiv:
            case cl::Bytecode::FloorDiv:
            case cl::Bytecode::BinaryPow:
            case cl::Bytecode::LShift:
            case cl::Bytecode::RShift:
            case cl::Bytecode::Mod:
            case cl::Bytecode::Or:
            case cl::Bytecode::And:
            case cl::Bytecode::Xor:
            case cl::Bytecode::TestEqual:
            case cl::Bytecode::TestNotEqual:
            case cl::Bytecode::TestLess:
            case cl::Bytecode::TestLessEqual:
            case cl::Bytecode::TestGreater:
            case cl::Bytecode::TestGreaterEqual:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_operator_cache(code_obj, out, pc++);
                assert(cl::Bytecode(code_obj.code[pc]) ==
                       cl::Bytecode::CheckOperatorNotImplemented);
                ++pc;
                break;

            case cl::Bytecode::TernaryPow:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_operator_cache(code_obj, out, pc++);
                assert(cl::Bytecode(code_obj.code[pc]) ==
                       cl::Bytecode::CheckTernaryOperatorNotImplemented);
                ++pc;
                break;

            case cl::Bytecode::AddSmi:
            case cl::Bytecode::SubSmi:
            case cl::Bytecode::MulSmi:
            case cl::Bytecode::FloorDivSmi:
            case cl::Bytecode::BinaryPowSmi:
            case cl::Bytecode::LShiftSmi:
            case cl::Bytecode::RShiftSmi:
            case cl::Bytecode::ModSmi:
            case cl::Bytecode::OrSmi:
            case cl::Bytecode::AndSmi:
            case cl::Bytecode::XorSmi:
                format_to(out, " ");
                disassemble_smi8(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_operator_cache(code_obj, out, pc++);
                assert(cl::Bytecode(code_obj.code[pc]) ==
                       cl::Bytecode::CheckOperatorNotImplemented);
                ++pc;
                break;

            case cl::Bytecode::Neg:
            case cl::Bytecode::Pos:
            case cl::Bytecode::Invert:
                format_to(out, " ");
                disassemble_operator_cache(code_obj, out, pc++);
                break;

            case cl::Bytecode::Nop:
            case cl::Bytecode::Not:
            case cl::Bytecode::ToBool:
            case cl::Bytecode::ToBoolNot:
            case cl::Bytecode::Sqrt:
            case cl::Bytecode::CanonicalizeHash:
            case cl::Bytecode::CheckOperatorNotImplemented:
            case cl::Bytecode::CheckTernaryOperatorNotImplemented:
                break;

            case cl::Bytecode::DictProbeStart:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", generation=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", hash_index=");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::DictProbeForLookup:
            case cl::Bytecode::DictProbeForInsert:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::DictProbeAdvance:
            case cl::Bytecode::DictEntryKey:
            case cl::Bytecode::DictEntryValue:
            case cl::Bytecode::DictResizeForInsert:
            case cl::Bytecode::DictPromoteStringKeyed:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::DictTryStringKeyedSetDefault:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", key=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", default=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", result=");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::DictTryStringKeyedPop:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", key=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", result=");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::DictEntryStillMatches:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::DictInsertNew:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", hash_index=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", first_tombstone_index=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", hash=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", key=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", value=");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::DictOverwriteEntry:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", entry=");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", value=");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::DictDeleteEntry:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", hash_index=");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::CallPositional:
                {
                    format_to(out, " ");
                    int8_t callable_reg = code_obj.code[pc++];
                    int8_t first_arg_reg = code_obj.code[pc++];
                    uint8_t n_args = code_obj.code[pc++];
                    uint32_t cache_idx_offset = pc++;
                    print_reg(out, code_obj, callable_reg);
                    format_to(out, ", ");
                    print_reg_span(out, code_obj, first_arg_reg, n_args);
                    format_to(out, ", ");
                    disassemble_function_call_cache(code_obj, out,
                                                    cache_idx_offset);
                }
                break;

            case cl::Bytecode::CallKeyword:
                {
                    format_to(out, " ");
                    int8_t callable_reg = code_obj.code[pc++];
                    int8_t first_arg_reg = code_obj.code[pc++];
                    uint8_t n_pos_args = code_obj.code[pc++];
                    int8_t first_kw_value_reg = code_obj.code[pc++];
                    uint8_t n_kw_args = code_obj.code[pc++];
                    uint32_t keyword_names_idx = pc++;
                    uint32_t cache_idx_offset = pc++;
                    print_reg(out, code_obj, callable_reg);
                    format_to(out, ", ");
                    print_reg_span(out, code_obj, first_arg_reg, n_pos_args);
                    format_to(out, ", kw_values=");
                    print_reg_span(out, code_obj, first_kw_value_reg,
                                   n_kw_args);
                    format_to(out, ", kw=");
                    uint8_t constant_idx = code_obj.code[keyword_names_idx];
                    cl::Value constant =
                        code_obj.constant_table[constant_idx].value();
                    if(cl::can_convert_to<cl::Tuple>(constant))
                    {
                        cl::format_tuple_constant(
                            out, constant.get_ptr<cl::Tuple>());
                    }
                    else
                    {
                        disassemble_constant(code_obj, out, keyword_names_idx);
                    }
                    format_to(out, ", ");
                    disassemble_function_call_cache(code_obj, out,
                                                    cache_idx_offset);
                }
                break;

            case cl::Bytecode::CallIntrinsic0:
            case cl::Bytecode::CallIntrinsic1:
            case cl::Bytecode::CallIntrinsic2:
            case cl::Bytecode::CallIntrinsic3:
            case cl::Bytecode::CallIntrinsic4:
            case cl::Bytecode::CallIntrinsic5:
            case cl::Bytecode::CallIntrinsic6:
            case cl::Bytecode::CallIntrinsic7:
            case cl::Bytecode::CallExtension0:
            case cl::Bytecode::CallExtension1:
            case cl::Bytecode::CallExtension2:
            case cl::Bytecode::CallExtension3:
            case cl::Bytecode::CallExtension4:
            case cl::Bytecode::CallExtension5:
            case cl::Bytecode::CallExtension6:
            case cl::Bytecode::CallExtension7:
                format_to(out, " {}", code_obj.code[pc++]);
                break;

            case cl::Bytecode::CallRuntimeIntrinsic0:
                format_to(out, " {}",
                          cl::RuntimeIntrinsic0(code_obj.code[pc++]));
                break;

            case cl::Bytecode::CallCodeObject:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", {}", code_obj.code[pc++]);
                break;

            case cl::Bytecode::ImportName:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", {}", code_obj.code[pc++]);
                break;

            case cl::Bytecode::ImportFrom:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                break;

            case cl::Bytecode::ForIter:
            case cl::Bytecode::ForPrepRange1:
            case cl::Bytecode::ForPrepRange2:
            case cl::Bytecode::ForPrepRange3:
            case cl::Bytecode::ForIterRange1:
            case cl::Bytecode::ForIterRangeStep:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_jump_target(code_obj, out, pc);
                pc += 2;
                break;

            case cl::Bytecode::CreateList:
            case cl::Bytecode::CreateTuple:
                format_to(out, " ");
                disassemble_reg_span(code_obj, out, pc);
                pc += 2;
                break;

            case cl::Bytecode::CreateDict:
                {
                    format_to(out, " ");
                    int8_t first_reg = static_cast<int8_t>(code_obj.code[pc++]);
                    uint8_t n_entries = code_obj.code[pc++];
                    print_reg_span(out, code_obj, first_reg,
                                   static_cast<uint8_t>(n_entries * 2));
                }
                break;

            case cl::Bytecode::CreateBinarySlice:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::CreateTernarySlice:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::CreateFunction:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                break;

            case cl::Bytecode::CreateFunctionWithDefaults:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::CreateClass:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::CreateInstanceKnownClass:
            case cl::Bytecode::IsInstanceOfKnownClass:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                break;

            case cl::Bytecode::BuildClass:
            case cl::Bytecode::CheckInitReturnedNone:
            case cl::Bytecode::WriteStdout:
            case cl::Bytecode::RaiseAssertionError:
            case cl::Bytecode::RaiseAssertionErrorWithMessage:
            case cl::Bytecode::RaiseUnwind:
            case cl::Bytecode::RaiseBare:
                break;
            case cl::Bytecode::RaiseUnwindWithContext:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::Jump:
            case cl::Bytecode::JumpIfTrue:
            case cl::Bytecode::JumpIfFalse:
                format_to(out, " ");
                disassemble_jump_target(code_obj, out, pc);
                pc += 2;
                break;
            case cl::Bytecode::JumpIfEqualSmi:
                format_to(out, " ");
                disassemble_smi8(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_jump_target(code_obj, out, pc);
                pc += 2;
                break;

            case cl::Bytecode::Return:
            case cl::Bytecode::ReturnOrRaiseException:
            case cl::Bytecode::ReturnToNative:
            case cl::Bytecode::ReturnExceptionMarkerToNative:
                break;

            case cl::Bytecode::LdaActiveException:
            case cl::Bytecode::ActiveExceptionIsInstance:
                break;

            case cl::Bytecode::DrainActiveExceptionInto:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::ClearActiveException:
            case cl::Bytecode::ReraiseActiveException:
                break;

            case cl::Bytecode::Invalid:
                break;
        }
        format_to(out, "\n");
        assert(pc == instruction.next_pc_offset());
        return instruction.next_pc_offset();
    }

    template <typename FormatContext>
    auto format(const cl::CodeObject &code_obj, FormatContext &ctx) const
        -> decltype(ctx.out())
    {
        auto &&out = ctx.out();
        format_to(out, "Code object:\n");

        for(uint32_t pc = 0; pc < code_obj.size();)
        {
            pc = disassemble_instruction(code_obj, out, pc);
        }

        if(!code_obj.exception_table.empty())
        {
            format_to(out, "Exception table:\n");
            for(const cl::ExceptionTableEntry &entry: code_obj.exception_table)
            {
                format_to(out, "    {}..{} -> {}\n", entry.start_pc,
                          entry.end_pc, entry.handler_pc);
            }
        }

        for(uint32_t cidx = 0; cidx < code_obj.constant_table.size(); ++cidx)
        {
            format_to(out, "Constant {}: ", cidx);
            cl::Value c = code_obj.constant_table[cidx].value();
            if(c.is_smi())
            {
                format_to(out, "{}", c.get_smi());
            }
            else if(c.is_ptr() && c.get_ptr<cl::Object>()->native_layout_id() ==
                                      cl::NativeLayoutId::CodeObject)
            {
                format_to(out, "{}", *c.get_ptr<cl::CodeObject>());
            }
            else if(c.is_ptr() && c.get_ptr<cl::Object>()->native_layout_id() ==
                                      cl::NativeLayoutId::String)
            {
                out = cl::format_string_constant(out, c.get_ptr<cl::String>());
            }
            else if(c.is_ptr() && c.get_ptr<cl::Object>()->native_layout_id() ==
                                      cl::NativeLayoutId::Tuple)
            {
                out = cl::format_tuple_constant(out, c.get_ptr<cl::Tuple>());
            }
            else if(c.is_ptr() && c.get_ptr<cl::Object>()->native_layout_id() ==
                                      cl::NativeLayoutId::ClassObject)
            {
                out = cl::format_class_constant(out,
                                                c.get_ptr<cl::ClassObject>());
            }
            format_to(out, "\n");
        }
        return out;
    }
};

#endif  // CL_CODE_OBJECT_PRINT_H
