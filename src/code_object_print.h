#ifndef CL_CODE_OBJECT_PRINT_H
#define CL_CODE_OBJECT_PRINT_H

#include "code_object.h"
#include <fmt/format.h>
#include <fmt/xchar.h>

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
            case cl::Bytecode::LoadSubscript:
                return format_to(out, "LoadSubscript");
            case cl::Bytecode::StoreSubscript:
                return format_to(out, "StoreSubscript");
            case cl::Bytecode::DelSubscript:
                return format_to(out, "DelSubscript");
            case cl::Bytecode::CallMethodAttr:
                return format_to(out, "CallMethodAttr");

            case cl::Bytecode::Add:
                return format_to(out, "Add");
            case cl::Bytecode::Sub:
                return format_to(out, "Sub");
            case cl::Bytecode::Mul:
                return format_to(out, "Mul");
            case cl::Bytecode::Div:
                return format_to(out, "Div");
            case cl::Bytecode::IntDiv:
                return format_to(out, "IntDiv");
            case cl::Bytecode::Pow:
                return format_to(out, "Pow");
            case cl::Bytecode::LeftShift:
                return format_to(out, "LeftShift");
            case cl::Bytecode::RightShift:
                return format_to(out, "RightShift");
            case cl::Bytecode::Mod:
                return format_to(out, "Mod");
            case cl::Bytecode::BitwiseOr:
                return format_to(out, "BitwiseOr");
            case cl::Bytecode::BitwiseAnd:
                return format_to(out, "BitwiseAnd");
            case cl::Bytecode::BitwiseXor:
                return format_to(out, "BitwiseXor");

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
            case cl::Bytecode::TestIn:
                return format_to(out, "TestIn");
            case cl::Bytecode::TestNotIn:
                return format_to(out, "TestNotIn");

            case cl::Bytecode::AddSmi:
                return format_to(out, "AddSmi");
            case cl::Bytecode::SubSmi:
                return format_to(out, "SubSmi");
            case cl::Bytecode::MulSmi:
                return format_to(out, "MulSmi");
            case cl::Bytecode::DivSmi:
                return format_to(out, "DivSmi");
            case cl::Bytecode::IntDivSmi:
                return format_to(out, "IntDivSmi");
            case cl::Bytecode::PowSmi:
                return format_to(out, "PowSmi");
            case cl::Bytecode::LeftShiftSmi:
                return format_to(out, "LeftShiftSmi");
            case cl::Bytecode::RightShiftSmi:
                return format_to(out, "RightShiftSmi");
            case cl::Bytecode::ModSmi:
                return format_to(out, "ModSmi");
            case cl::Bytecode::BitwiseOrSmi:
                return format_to(out, "BitwiseOrSmi");
            case cl::Bytecode::BitwiseAndSmi:
                return format_to(out, "BitwiseAndSmi");
            case cl::Bytecode::BitwiseXorSmi:
                return format_to(out, "BitwiseXorSmi");

            case cl::Bytecode::Nop:
                return format_to(out, "Nop");

            case cl::Bytecode::Not:
                return format_to(out, "Not");
            case cl::Bytecode::Negate:
                return format_to(out, "Negate");
            case cl::Bytecode::Plus:
                return format_to(out, "Plus");
            case cl::Bytecode::BitwiseNot:
                return format_to(out, "BitwiseNot");

            case cl::Bytecode::CallSimple:
                return format_to(out, "CallSimple");
            case cl::Bytecode::CallNative0:
                return format_to(out, "CallNative0");
            case cl::Bytecode::CallNative1:
                return format_to(out, "CallNative1");
            case cl::Bytecode::CallNative2:
                return format_to(out, "CallNative2");
            case cl::Bytecode::CallNative3:
                return format_to(out, "CallNative3");
            case cl::Bytecode::CallCodeObject:
                return format_to(out, "CallCodeObject");
            case cl::Bytecode::GetIter:
                return format_to(out, "GetIter");
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
            case cl::Bytecode::CreateList:
                return format_to(out, "CreateList");
            case cl::Bytecode::CreateTuple:
                return format_to(out, "CreateTuple");
            case cl::Bytecode::BuildClass:
                return format_to(out, "BuildClass");
            case cl::Bytecode::CheckInitReturnedNone:
                return format_to(out, "CheckInitReturnedNone");
            case cl::Bytecode::RaiseAssertionError:
                return format_to(out, "RaiseAssertionError");
            case cl::Bytecode::RaiseAssertionErrorWithMessage:
                return format_to(out, "RaiseAssertionErrorWithMessage");
            case cl::Bytecode::RaiseUnwind:
                return format_to(out, "RaiseUnwind");
            case cl::Bytecode::RaiseUnwindWithContext:
                return format_to(out, "RaiseUnwindWithContext");

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
            case cl::Bytecode::RaiseIfUnhandledException:
                return format_to(out, "RaiseIfUnhandledException");
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
            case cl::Bytecode::Halt:
                return format_to(out, "Halt");

            case cl::Bytecode::Invalid:
                return format_to(out, "Invalid");
        }
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
            uint32_t n_ordinary_slots =
                code_obj.get_padded_n_ordinary_below_frame_slots();
            if(frame_slot >= n_ordinary_slots &&
               frame_slot < n_ordinary_slots + code_obj.n_outgoing_call_slots)
            {
                format_to(out, "a{}", frame_slot - n_ordinary_slots);
            }
            else
            {
                format_to(out, "r{}", frame_slot);
            }
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
    void disassemble_function_call_cache(const cl::CodeObject &code_obj,
                                         Out &out, uint32_t pc) const
    {
        format_to(out, "call_ic[{}]", code_obj.code[pc]);
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

    static uint32_t read_uint32_le(const uint8_t *p)
    {
        return (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    }

    template <typename Out>
    void disassemble_global_ref(const cl::CodeObject &code_obj, Out &out,
                                uint32_t pc) const
    {
        uint32_t v = read_uint32_le(&code_obj.code[pc]);
        format_to(out, "[{}]", v);
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
            case cl::Bytecode::StaGlobal:
            case cl::Bytecode::DelGlobal:
                format_to(out, " ");
                disassemble_global_ref(code_obj, out, pc);
                pc += 4;
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

            case cl::Bytecode::LoadSubscript:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::StoreSubscript:
            case cl::Bytecode::DelSubscript:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::CallMethodAttr:
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

            case cl::Bytecode::Add:
            case cl::Bytecode::Sub:
            case cl::Bytecode::Mul:
            case cl::Bytecode::Div:
            case cl::Bytecode::IntDiv:
            case cl::Bytecode::Pow:
            case cl::Bytecode::LeftShift:
            case cl::Bytecode::RightShift:
            case cl::Bytecode::Mod:
            case cl::Bytecode::BitwiseOr:
            case cl::Bytecode::BitwiseAnd:
            case cl::Bytecode::BitwiseXor:
            case cl::Bytecode::TestEqual:
            case cl::Bytecode::TestNotEqual:
            case cl::Bytecode::TestLess:
            case cl::Bytecode::TestLessEqual:
            case cl::Bytecode::TestGreater:
            case cl::Bytecode::TestGreaterEqual:
            case cl::Bytecode::TestIs:
            case cl::Bytecode::TestIsNot:
            case cl::Bytecode::TestIn:
            case cl::Bytecode::TestNotIn:
                format_to(out, " ");
                disassemble_reg(code_obj, out, pc++);
                break;

            case cl::Bytecode::AddSmi:
            case cl::Bytecode::SubSmi:
            case cl::Bytecode::MulSmi:
            case cl::Bytecode::DivSmi:
            case cl::Bytecode::IntDivSmi:
            case cl::Bytecode::PowSmi:
            case cl::Bytecode::LeftShiftSmi:
            case cl::Bytecode::RightShiftSmi:
            case cl::Bytecode::ModSmi:
            case cl::Bytecode::BitwiseOrSmi:
            case cl::Bytecode::BitwiseAndSmi:
            case cl::Bytecode::BitwiseXorSmi:
                format_to(out, " ");
                disassemble_smi8(code_obj, out, pc++);
                break;

            case cl::Bytecode::Nop:
            case cl::Bytecode::Not:
            case cl::Bytecode::Negate:
            case cl::Bytecode::Plus:
            case cl::Bytecode::BitwiseNot:
                break;

            case cl::Bytecode::CallSimple:
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

            case cl::Bytecode::CallNative0:
            case cl::Bytecode::CallNative1:
            case cl::Bytecode::CallNative2:
            case cl::Bytecode::CallNative3:
                format_to(out, " {}", code_obj.code[pc++]);
                break;

            case cl::Bytecode::CallCodeObject:
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                format_to(out, ", ");
                disassemble_reg(code_obj, out, pc++);
                format_to(out, ", {}", code_obj.code[pc++]);
                break;

            case cl::Bytecode::GetIter:
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
                format_to(out, " ");
                disassemble_constant(code_obj, out, pc++);
                break;

            case cl::Bytecode::BuildClass:
            case cl::Bytecode::CheckInitReturnedNone:
            case cl::Bytecode::RaiseAssertionError:
            case cl::Bytecode::RaiseAssertionErrorWithMessage:
            case cl::Bytecode::RaiseUnwind:
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
                break;

            case cl::Bytecode::RaiseIfUnhandledException:
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

            case cl::Bytecode::Halt:
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
            cl::Value c = code_obj.constant_table[cidx].as_value();
            if(c.is_smi())
            {
                format_to(out, "{}", c.get_smi());
            }
            else if(c.is_ptr() && c.get_ptr<cl::Object>()->native_layout_id() ==
                                      cl::NativeLayoutId::CodeObject)
            {
                format_to(out, "{}", *c.get_ptr<cl::CodeObject>());
            }
            format_to(out, "\n");
        }
        return out;
    }
};

#endif  // CL_CODE_OBJECT_PRINT_H
