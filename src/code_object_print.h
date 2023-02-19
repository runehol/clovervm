#ifndef CL_CODE_OBJECT_PRINT_H
#define CL_CODE_OBJECT_PRINT_H

#include "code_object.h"
#include <fmt/format.h>
#include <fmt/xchar.h>


template<>
struct fmt::formatter<cl::Bytecode>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(const cl::Bytecode b, FormatContext& ctx) -> decltype(ctx.out())
    {
        auto&& out = ctx.out();
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
        case cl::Bytecode::Star:
            return format_to(out, "Star");


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

        case cl::Bytecode::Return:
            return format_to(out, "Return");

        case cl::Bytecode::Invalid:
            return format_to(out, "Invalid");


        }
    }
};

template<>
struct fmt::formatter<cl::CodeObject>
{
    constexpr auto parse(format_parse_context& ctx)
    {
        return ctx.end();
    }

    template <typename Out>
    void disassemble_reg(const cl::CodeObject &code_obj, Out &out, uint32_t pc)
    {
        format_to(out, "r{}", code_obj.code[pc]);
    }

    template <typename Out>
    void disassemble_constant(const cl::CodeObject &code_obj, Out &out, uint32_t pc)
    {
        format_to(out, "c[{}}", code_obj.code[pc]);
    }

    template <typename Out>
    void disassemble_smi8(const cl::CodeObject &code_obj, Out &out, uint32_t pc)
    {
        int32_t smi = int8_t(code_obj.code[pc]);
        format_to(out, "{}", smi);

    }

    template <typename Out>
    uint32_t disassemble_instruction(const cl::CodeObject &code_obj, Out &out, uint32_t pc)
    {

        cl::Bytecode bc = cl::Bytecode(code_obj.code[pc++]);
        format_to(out, "{:05d} {}", code_obj.source_offsets[pc], bc);


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
        case cl::Bytecode::Star:
            format_to(out, " ");
            disassemble_reg(code_obj, out, pc++);
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

        case cl::Bytecode::Return:
            break;

        case cl::Bytecode::Invalid:
            break;

        }
        format_to(out, "\n");
        return pc;
    }

    template <typename FormatContext>
    auto format(const cl::CodeObject &code_obj, FormatContext& ctx) -> decltype(ctx.out())
    {
        auto&& out = ctx.out();
        format_to(out, "Code object:\n");

        for(uint32_t pc = 0; pc < code_obj.size(); )
        {
            pc = disassemble_instruction(code_obj, out, pc);
        }
        return out;
    }
};


#endif //CL_CODE_OBJECT_PRINT_H
