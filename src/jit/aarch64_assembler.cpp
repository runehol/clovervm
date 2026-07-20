#include "jit/aarch64_assembler.h"

#include <cassert>
#include <limits>

namespace cl::jit
{
    namespace
    {
        constexpr MoveWideHalfword move_wide_halfword(uint32_t index)
        {
            assert(index < 4);
            return static_cast<MoveWideHalfword>(index << 21);
        }

        void emit_mov(AArch64EmitterAssembler &assembler,
                      XRegisterOrZero destination, uint64_t immediate)
        {
            bool emitted = false;
            for(uint32_t halfword = 0; halfword < 4; ++halfword)
            {
                uint16_t part = static_cast<uint16_t>(
                    immediate >>
                    (halfword * std::numeric_limits<uint16_t>::digits));
                if(part == 0)
                {
                    continue;
                }
                MoveWideHalfword position = move_wide_halfword(halfword);
                if(!emitted)
                {
                    assembler.emit_move_wide_imm16(MoveWideOp::Movz,
                                                   destination, part, position);
                    emitted = true;
                }
                else
                {
                    assembler.emit_move_wide_imm16(MoveWideOp::Movk,
                                                   destination, part, position);
                }
            }
            if(!emitted)
            {
                assembler.emit_move_wide_imm16(MoveWideOp::Movz, destination,
                                               0);
            }
        }

        void emit_mov(AArch64EmitterAssembler &assembler,
                      WRegisterOrZero destination, uint32_t immediate)
        {
            uint16_t low = static_cast<uint16_t>(immediate);
            uint16_t high = static_cast<uint16_t>(
                immediate >> std::numeric_limits<uint16_t>::digits);
            if(low != 0 || high == 0)
            {
                assembler.emit_move_wide_imm16(MoveWideOp::Movz, destination,
                                               low);
                if(high != 0)
                {
                    assembler.emit_move_wide_imm16(MoveWideOp::Movk,
                                                   destination, high,
                                                   MoveWideHalfword::Bits16);
                }
            }
            else
            {
                assembler.emit_move_wide_imm16(MoveWideOp::Movz, destination,
                                               high, MoveWideHalfword::Bits16);
            }
        }
    }  // namespace

    uint32_t AArch64DirectBranch::select(MachineAddress source,
                                         MachineAddress target)
    {
        int64_t displacement = source.displacement_to(target);
        direct_ = aarch64_detail::fits_signed_scaled_displacement(displacement,
                                                                  26, 2);
        if(*direct_)
        {
            return 4;
        }
        return max_size();
    }

    void AArch64DirectBranch::encode(void *write_pointer, MachineAddress source,
                                     MachineAddress target) const
    {
        assert(direct_.has_value());
        AArch64BufferAssembler assembler(write_pointer);
        if(*direct_)
        {
            int64_t displacement = source.displacement_to(target);
            if(kind_ == AArch64BranchKind::Call)
            {
                assembler.emit_bl_immediate_26(displacement);
            }
            else
            {
                assembler.emit_b_immediate_26(displacement);
            }
            return;
        }

        uintptr_t address = target.bits_for_indirect_target();
        assembler.emit_move_wide_imm16(MoveWideOp::Movz, scratch_,
                                       static_cast<uint16_t>(address),
                                       MoveWideHalfword::Bits0);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, scratch_,
                                       static_cast<uint16_t>(address >> 16),
                                       MoveWideHalfword::Bits16);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, scratch_,
                                       static_cast<uint16_t>(address >> 32),
                                       MoveWideHalfword::Bits32);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, scratch_,
                                       static_cast<uint16_t>(address >> 48),
                                       MoveWideHalfword::Bits48);
        if(kind_ == AArch64BranchKind::Call)
        {
            assembler.emit_blr(scratch_);
        }
        else
        {
            assembler.emit_br(scratch_);
        }
    }

    void AArch64Relocation::apply(void *write_pointer,
                                  MachineAddress instruction_pc,
                                  MachineAddress target) const
    {
        AArch64BufferAssembler assembler(write_pointer);
        if(mode_ == AArch64ValuePoolMode::NearLiteral)
        {
            assembler.emit_ldr_literal_immediate_19(
                destination_, instruction_pc.displacement_to(target));
            return;
        }

        int64_t page_displacement =
            instruction_pc.aligned_displacement_to(target, 12);
        assembler.emit_adrp_page_immediate_21(destination_, page_displacement);
        assembler.emit_ldr_unsigned_offset(
            destination_, destination_,
            static_cast<uint16_t>(target.offset_within(12)));
    }

    void AArch64MacroAssembler::mov(XRegisterOrZero destination,
                                    uint64_t immediate)
    {
        emit_mov(*this, destination, immediate);
    }

    void AArch64MacroAssembler::mov(WRegisterOrZero destination,
                                    uint32_t immediate)
    {
        emit_mov(*this, destination, immediate);
    }

    void AArch64MacroAssembler::mov(XRegisterOrZero destination,
                                    XRegisterOrZero source)
    {
        emit_logical_reg(LogicalOp::Orr, destination, xzr, source);
    }

    void AArch64MacroAssembler::mov(WRegisterOrZero destination,
                                    WRegisterOrZero source)
    {
        emit_logical_reg(LogicalOp::Orr, destination, wzr, source);
    }

    void AArch64MacroAssembler::mvn(XRegisterOrZero destination,
                                    XRegisterOrZero source)
    {
        emit_logical_reg(LogicalOp::Orr, destination, xzr, source,
                         InvertMode::Invert);
    }

    void AArch64MacroAssembler::mvn(WRegisterOrZero destination,
                                    WRegisterOrZero source)
    {
        emit_logical_reg(LogicalOp::Orr, destination, wzr, source,
                         InvertMode::Invert);
    }

    void AArch64MacroAssembler::neg(XRegisterOrZero destination,
                                    XRegisterOrZero source)
    {
        emit_arithmetic_reg(ArithmeticOp::Sub, destination, xzr, source);
    }

    void AArch64MacroAssembler::neg(WRegisterOrZero destination,
                                    WRegisterOrZero source)
    {
        emit_arithmetic_reg(ArithmeticOp::Sub, destination, wzr, source);
    }

    void AArch64MacroAssembler::cmp(XRegisterOrZero left, XRegisterOrZero right)
    {
        emit_arithmetic_reg(ArithmeticOp::Subs, xzr, left, right);
    }

    void AArch64MacroAssembler::cmp(WRegisterOrZero left, WRegisterOrZero right)
    {
        emit_arithmetic_reg(ArithmeticOp::Subs, wzr, left, right);
    }

    void AArch64MacroAssembler::cmn(XRegisterOrZero left, XRegisterOrZero right)
    {
        emit_arithmetic_reg(ArithmeticOp::Adds, xzr, left, right);
    }

    void AArch64MacroAssembler::cmn(WRegisterOrZero left, WRegisterOrZero right)
    {
        emit_arithmetic_reg(ArithmeticOp::Adds, wzr, left, right);
    }

    void AArch64MacroAssembler::ldr(XRegister destination, Value value)
    {
        ValuePoolEntry entry = emitter().add_value_to_constant_pool(value);
        uint32_t instructions[2] = {};
        size_t size = pool_mode_ == AArch64ValuePoolMode::NearLiteral
                          ? sizeof(uint32_t)
                          : sizeof(instructions);
        emitter().emit_relocatable(
            instructions, size,
            AArch64Relocation(entry, destination, pool_mode_));
    }

    void AArch64MacroAssembler::b(CodeTarget target, XRegister scratch)
    {
        emitter().emit_direct_branch(
            AArch64DirectBranch(target, AArch64BranchKind::Jump, scratch));
    }

    void AArch64MacroAssembler::bl(CodeTarget target, XRegister scratch)
    {
        emitter().emit_direct_branch(
            AArch64DirectBranch(target, AArch64BranchKind::Call, scratch));
    }

}  // namespace cl::jit
