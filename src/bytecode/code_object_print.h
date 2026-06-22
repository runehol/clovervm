#ifndef CL_CODE_OBJECT_PRINT_H
#define CL_CODE_OBJECT_PRINT_H

#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
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
        auto &&out = ctx.out();
        switch(b)
        {
            case cl::Bytecode::LdaConstant:
                return format_to(out, "LdaConstant");
            case cl::Bytecode::LdaSmi:
                return format_to(out, "LdaSmi");
            case cl::Bytecode::LdaTrue:
                return format_to(out, "LdaTrue");
            case cl::Bytecode::LdaFalse:
                return format_to(out, "LdaFalse");
            case cl::Bytecode::LdaNone:
                return format_to(out, "LdaNone");

            case cl::Bytecode::Ldar:
                return format_to(out, "Ldar");
            case cl::Bytecode::LoadLocalChecked:
                return format_to(out, "LoadLocalChecked");
            case cl::Bytecode::ClearLocal:
                return format_to(out, "ClearLocal");
            case cl::Bytecode::Star:
                return format_to(out, "Star");
            case cl::Bytecode::Mov:
                return format_to(out, "Mov");
            case cl::Bytecode::Ldar0:
                return format_to(out, "Ldar0");
            case cl::Bytecode::Ldar1:
                return format_to(out, "Ldar1");
            case cl::Bytecode::Ldar2:
                return format_to(out, "Ldar2");
            case cl::Bytecode::Ldar3:
                return format_to(out, "Ldar3");
            case cl::Bytecode::Ldar4:
                return format_to(out, "Ldar4");
            case cl::Bytecode::Ldar5:
                return format_to(out, "Ldar5");
            case cl::Bytecode::Ldar6:
                return format_to(out, "Ldar6");
            case cl::Bytecode::Ldar7:
                return format_to(out, "Ldar7");
            case cl::Bytecode::Ldar8:
                return format_to(out, "Ldar8");
            case cl::Bytecode::Ldar9:
                return format_to(out, "Ldar9");
            case cl::Bytecode::Ldar10:
                return format_to(out, "Ldar10");
            case cl::Bytecode::Ldar11:
                return format_to(out, "Ldar11");
            case cl::Bytecode::Ldar12:
                return format_to(out, "Ldar12");
            case cl::Bytecode::Ldar13:
                return format_to(out, "Ldar13");
            case cl::Bytecode::Ldar14:
                return format_to(out, "Ldar14");
            case cl::Bytecode::Ldar15:
                return format_to(out, "Ldar15");

            case cl::Bytecode::Star0:
                return format_to(out, "Star0");
            case cl::Bytecode::Star1:
                return format_to(out, "Star1");
            case cl::Bytecode::Star2:
                return format_to(out, "Star2");
            case cl::Bytecode::Star3:
                return format_to(out, "Star3");
            case cl::Bytecode::Star4:
                return format_to(out, "Star4");
            case cl::Bytecode::Star5:
                return format_to(out, "Star5");
            case cl::Bytecode::Star6:
                return format_to(out, "Star6");
            case cl::Bytecode::Star7:
                return format_to(out, "Star7");
            case cl::Bytecode::Star8:
                return format_to(out, "Star8");
            case cl::Bytecode::Star9:
                return format_to(out, "Star9");
            case cl::Bytecode::Star10:
                return format_to(out, "Star10");
            case cl::Bytecode::Star11:
                return format_to(out, "Star11");
            case cl::Bytecode::Star12:
                return format_to(out, "Star12");
            case cl::Bytecode::Star13:
                return format_to(out, "Star13");
            case cl::Bytecode::Star14:
                return format_to(out, "Star14");
            case cl::Bytecode::Star15:
                return format_to(out, "Star15");

            case cl::Bytecode::LdaGlobal:
                return format_to(out, "LdaGlobal");
            case cl::Bytecode::StaGlobal:
                return format_to(out, "StaGlobal");
            case cl::Bytecode::DelGlobal:
                return format_to(out, "DelGlobal");
            case cl::Bytecode::DelLocal:
                return format_to(out, "DelLocal");
            case cl::Bytecode::LoadAttr:
                return format_to(out, "LoadAttr");
            case cl::Bytecode::StoreAttr:
                return format_to(out, "StoreAttr");
            case cl::Bytecode::DelAttr:
                return format_to(out, "DelAttr");
            case cl::Bytecode::GetItem:
                return format_to(out, "GetItem");
            case cl::Bytecode::SetItem:
                return format_to(out, "SetItem");
            case cl::Bytecode::DelItem:
                return format_to(out, "DelItem");
            case cl::Bytecode::CallMethodAttrPositional:
                return format_to(out, "CallMethodAttrPositional");
            case cl::Bytecode::CallMethodAttrKeyword:
                return format_to(out, "CallMethodAttrKeyword");
            case cl::Bytecode::CallSpecialMethod:
                return format_to(out, "CallSpecialMethod");

            case cl::Bytecode::Add:
                return format_to(out, "Add");
            case cl::Bytecode::Sub:
                return format_to(out, "Sub");
            case cl::Bytecode::Mul:
                return format_to(out, "Mul");
            case cl::Bytecode::TrueDiv:
                return format_to(out, "TrueDiv");
            case cl::Bytecode::FloorDiv:
                return format_to(out, "FloorDiv");
            case cl::Bytecode::BinaryPow:
                return format_to(out, "BinaryPow");
            case cl::Bytecode::TernaryPow:
                return format_to(out, "TernaryPow");
            case cl::Bytecode::LShift:
                return format_to(out, "LShift");
            case cl::Bytecode::RShift:
                return format_to(out, "RShift");
            case cl::Bytecode::Mod:
                return format_to(out, "Mod");
            case cl::Bytecode::Or:
                return format_to(out, "Or");
            case cl::Bytecode::And:
                return format_to(out, "And");
            case cl::Bytecode::Xor:
                return format_to(out, "Xor");

            case cl::Bytecode::TestEqual:
                return format_to(out, "TestEqual");
            case cl::Bytecode::TestNotEqual:
                return format_to(out, "TestNotEqual");
            case cl::Bytecode::TestLess:
                return format_to(out, "TestLess");
            case cl::Bytecode::TestLessEqual:
                return format_to(out, "TestLessEqual");
            case cl::Bytecode::TestGreater:
                return format_to(out, "TestGreater");
            case cl::Bytecode::TestGreaterEqual:
                return format_to(out, "TestGreaterEqual");
            case cl::Bytecode::TestIs:
                return format_to(out, "TestIs");
            case cl::Bytecode::TestIsNot:
                return format_to(out, "TestIsNot");
            case cl::Bytecode::Contains:
                return format_to(out, "Contains");
            case cl::Bytecode::CheckOperatorNotImplemented:
                return format_to(out, "CheckOperatorNotImplemented");
            case cl::Bytecode::CheckTernaryOperatorNotImplemented:
                return format_to(out, "CheckTernaryOperatorNotImplemented");

            case cl::Bytecode::AddSmi:
                return format_to(out, "AddSmi");
            case cl::Bytecode::SubSmi:
                return format_to(out, "SubSmi");
            case cl::Bytecode::MulSmi:
                return format_to(out, "MulSmi");
            case cl::Bytecode::FloorDivSmi:
                return format_to(out, "FloorDivSmi");
            case cl::Bytecode::BinaryPowSmi:
                return format_to(out, "BinaryPowSmi");
            case cl::Bytecode::LShiftSmi:
                return format_to(out, "LShiftSmi");
            case cl::Bytecode::RShiftSmi:
                return format_to(out, "RShiftSmi");
            case cl::Bytecode::ModSmi:
                return format_to(out, "ModSmi");
            case cl::Bytecode::OrSmi:
                return format_to(out, "OrSmi");
            case cl::Bytecode::AndSmi:
                return format_to(out, "AndSmi");
            case cl::Bytecode::XorSmi:
                return format_to(out, "XorSmi");

            case cl::Bytecode::Nop:
                return format_to(out, "Nop");

            case cl::Bytecode::Not:
                return format_to(out, "Not");
            case cl::Bytecode::ToBool:
                return format_to(out, "ToBool");
            case cl::Bytecode::ToBoolNot:
                return format_to(out, "ToBoolNot");
            case cl::Bytecode::Neg:
                return format_to(out, "Neg");
            case cl::Bytecode::Pos:
                return format_to(out, "Pos");
            case cl::Bytecode::Sqrt:
                return format_to(out, "Sqrt");
            case cl::Bytecode::Invert:
                return format_to(out, "Invert");

            case cl::Bytecode::CallPositional:
                return format_to(out, "CallPositional");
            case cl::Bytecode::CallKeyword:
                return format_to(out, "CallKeyword");
            case cl::Bytecode::CallIntrinsic0:
                return format_to(out, "CallIntrinsic0");
            case cl::Bytecode::CallIntrinsic1:
                return format_to(out, "CallIntrinsic1");
            case cl::Bytecode::CallIntrinsic2:
                return format_to(out, "CallIntrinsic2");
            case cl::Bytecode::CallIntrinsic3:
                return format_to(out, "CallIntrinsic3");
            case cl::Bytecode::CallIntrinsic4:
                return format_to(out, "CallIntrinsic4");
            case cl::Bytecode::CallIntrinsic5:
                return format_to(out, "CallIntrinsic5");
            case cl::Bytecode::CallIntrinsic6:
                return format_to(out, "CallIntrinsic6");
            case cl::Bytecode::CallIntrinsic7:
                return format_to(out, "CallIntrinsic7");
            case cl::Bytecode::CallExtension0:
                return format_to(out, "CallExtension0");
            case cl::Bytecode::CallExtension1:
                return format_to(out, "CallExtension1");
            case cl::Bytecode::CallExtension2:
                return format_to(out, "CallExtension2");
            case cl::Bytecode::CallExtension3:
                return format_to(out, "CallExtension3");
            case cl::Bytecode::CallExtension4:
                return format_to(out, "CallExtension4");
            case cl::Bytecode::CallExtension5:
                return format_to(out, "CallExtension5");
            case cl::Bytecode::CallExtension6:
                return format_to(out, "CallExtension6");
            case cl::Bytecode::CallExtension7:
                return format_to(out, "CallExtension7");
            case cl::Bytecode::CallRuntimeIntrinsic0:
                return format_to(out, "CallRuntimeIntrinsic0");
            case cl::Bytecode::CallCodeObject:
                return format_to(out, "CallCodeObject");
            case cl::Bytecode::ImportName:
                return format_to(out, "ImportName");
            case cl::Bytecode::ImportFrom:
                return format_to(out, "ImportFrom");
            case cl::Bytecode::ForIter:
                return format_to(out, "ForIter");
            case cl::Bytecode::ForPrepRange1:
                return format_to(out, "ForPrepRange1");
            case cl::Bytecode::ForPrepRange2:
                return format_to(out, "ForPrepRange2");
            case cl::Bytecode::ForPrepRange3:
                return format_to(out, "ForPrepRange3");
            case cl::Bytecode::ForIterRange1:
                return format_to(out, "ForIterRange1");
            case cl::Bytecode::ForIterRangeStep:
                return format_to(out, "ForIterRangeStep");

            case cl::Bytecode::CreateDict:
                return format_to(out, "CreateDict");
            case cl::Bytecode::CreateFunction:
                return format_to(out, "CreateFunction");
            case cl::Bytecode::CreateFunctionWithDefaults:
                return format_to(out, "CreateFunctionWithDefaults");
            case cl::Bytecode::CreateClass:
                return format_to(out, "CreateClass");
            case cl::Bytecode::CreateInstanceKnownClass:
                return format_to(out, "CreateInstanceKnownClass");
            case cl::Bytecode::IsInstanceOfKnownClass:
                return format_to(out, "IsInstanceOfKnownClass");
            case cl::Bytecode::CreateList:
                return format_to(out, "CreateList");
            case cl::Bytecode::CreateBinarySlice:
                return format_to(out, "CreateBinarySlice");
            case cl::Bytecode::CreateTernarySlice:
                return format_to(out, "CreateTernarySlice");
            case cl::Bytecode::CreateTuple:
                return format_to(out, "CreateTuple");
            case cl::Bytecode::BuildClass:
                return format_to(out, "BuildClass");
            case cl::Bytecode::CheckInitReturnedNone:
                return format_to(out, "CheckInitReturnedNone");
            case cl::Bytecode::WriteStdout:
                return format_to(out, "WriteStdout");
            case cl::Bytecode::RaiseAssertionError:
                return format_to(out, "RaiseAssertionError");
            case cl::Bytecode::RaiseAssertionErrorWithMessage:
                return format_to(out, "RaiseAssertionErrorWithMessage");
            case cl::Bytecode::RaiseUnwind:
                return format_to(out, "RaiseUnwind");
            case cl::Bytecode::RaiseUnwindWithContext:
                return format_to(out, "RaiseUnwindWithContext");
            case cl::Bytecode::RaiseBare:
                return format_to(out, "RaiseBare");

            case cl::Bytecode::Jump:
                return format_to(out, "Jump");
            case cl::Bytecode::JumpIfTrue:
                return format_to(out, "JumpIfTrue");
            case cl::Bytecode::JumpIfFalse:
                return format_to(out, "JumpIfFalse");
            case cl::Bytecode::Return:
                return format_to(out, "Return");
            case cl::Bytecode::ReturnOrRaiseException:
                return format_to(out, "ReturnOrRaiseException");
            case cl::Bytecode::ReturnToNative:
                return format_to(out, "ReturnToNative");
            case cl::Bytecode::ReturnExceptionMarkerToNative:
                return format_to(out, "ReturnExceptionMarkerToNative");
            case cl::Bytecode::LdaActiveException:
                return format_to(out, "LdaActiveException");
            case cl::Bytecode::ActiveExceptionIsInstance:
                return format_to(out, "ActiveExceptionIsInstance");
            case cl::Bytecode::DrainActiveExceptionInto:
                return format_to(out, "DrainActiveExceptionInto");
            case cl::Bytecode::ClearActiveException:
                return format_to(out, "ClearActiveException");
            case cl::Bytecode::ReraiseActiveException:
                return format_to(out, "ReraiseActiveException");

            case cl::Bytecode::Invalid:
                return format_to(out, "Invalid");
        }
        return format_to(out, "<unknown>");
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
                                     uint32_t pc) const
    {

        cl::Bytecode bc = cl::Bytecode(code_obj.code[pc]);
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

            case cl::Bytecode::CallSpecialMethod:
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
            case cl::Bytecode::CheckOperatorNotImplemented:
            case cl::Bytecode::CheckTernaryOperatorNotImplemented:
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
        return pc;
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
