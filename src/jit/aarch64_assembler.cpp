#include "jit/aarch64_assembler.h"

#include <cassert>
#include <cstring>
#include <limits>

namespace cl::jit
{
    namespace
    {
        constexpr uint32_t register_field(uint32_t encoding, uint8_t shift)
        {
            return encoding << shift;
        }

        template <typename Encoding>
        constexpr uint32_t encoding_bits(Encoding encoding)
        {
            return static_cast<uint32_t>(encoding);
        }

        constexpr MoveWideHalfword move_wide_halfword(uint32_t index)
        {
            assert(index < 4);
            return static_cast<MoveWideHalfword>(index << 21);
        }

        bool fits_signed_scaled_displacement(int64_t displacement,
                                             uint8_t immediate_bits,
                                             uint8_t scale_shift)
        {
            int64_t scale = int64_t{1} << scale_shift;
            if(displacement % scale != 0)
            {
                return false;
            }
            int64_t scaled = displacement / scale;
            int64_t minimum = -(int64_t{1} << (immediate_bits - 1));
            int64_t maximum = (int64_t{1} << (immediate_bits - 1)) - 1;
            return scaled >= minimum && scaled <= maximum;
        }

        uint32_t signed_immediate(int64_t displacement, uint8_t immediate_bits,
                                  uint8_t scale_shift)
        {
            assert(fits_signed_scaled_displacement(displacement, immediate_bits,
                                                   scale_shift));
            uint64_t mask = (uint64_t{1} << immediate_bits) - 1;
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(displacement >> scale_shift)) & mask);
        }

        void emit_mov(AArch64Assembler &assembler, XRegisterOrZero destination,
                      uint64_t immediate)
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

        void emit_mov(AArch64Assembler &assembler, WRegisterOrZero destination,
                      uint32_t immediate)
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
        direct_ = fits_signed_scaled_displacement(displacement, 26, 2);
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
        AArch64Assembler assembler(write_pointer);
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
        AArch64Assembler assembler(write_pointer);
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

    void AArch64Assembler::write_instruction(uint32_t instruction)
    {
        if(emitter_.has_value())
        {
            emitter_->emit_bytes(&instruction, sizeof(instruction));
            return;
        }
        assert(output_ != nullptr);
        std::memcpy(output_, &instruction, sizeof(instruction));
        output_ += sizeof(instruction);
    }

    void AArch64Assembler::emit_arithmetic_imm12(
        GPRWidth width, ArithmeticOp operation, uint32_t destination,
        uint32_t source, uint16_t immediate, AddImmediateShift shift)
    {
        write_instruction(0x11000000 | encoding_bits(width) |
                          encoding_bits(operation) | encoding_bits(shift) |
                          (static_cast<uint32_t>(immediate) << 10) |
                          register_field(source, 5) | destination);
    }

    void AArch64Assembler::emit_arithmetic_reg(
        GPRWidth width, ArithmeticOp operation, uint32_t destination,
        uint32_t source1, uint32_t source2, ArithmeticShift shift,
        uint8_t shift_amount)
    {
        write_instruction(0x0b000000 | encoding_bits(width) |
                          encoding_bits(operation) | encoding_bits(shift) |
                          register_field(source2, 16) |
                          (static_cast<uint32_t>(shift_amount) << 10) |
                          register_field(source1, 5) | destination);
    }

    void AArch64Assembler::emit_logical_reg(GPRWidth width, LogicalOp operation,
                                            uint32_t destination,
                                            uint32_t source1, uint32_t source2,
                                            InvertMode invert,
                                            LogicalShift shift,
                                            uint8_t shift_amount)
    {
        write_instruction(0x0a000000 | encoding_bits(width) |
                          encoding_bits(operation) | encoding_bits(invert) |
                          encoding_bits(shift) | register_field(source2, 16) |
                          (static_cast<uint32_t>(shift_amount) << 10) |
                          register_field(source1, 5) | destination);
    }

    void AArch64Assembler::emit_move_wide_imm16(GPRWidth width,
                                                MoveWideOp operation,
                                                uint32_t destination,
                                                uint16_t immediate,
                                                MoveWideHalfword halfword)
    {
        write_instruction(0x12800000 | encoding_bits(width) |
                          encoding_bits(operation) | encoding_bits(halfword) |
                          (static_cast<uint32_t>(immediate) << 5) |
                          destination);
    }

    void AArch64Assembler::emit_ldr_unsigned_offset(GPRWidth width,
                                                    uint32_t destination,
                                                    uint32_t base,
                                                    uint32_t scaled_offset)
    {
        write_instruction(0xb9400000 | (encoding_bits(width) >> 1) |
                          (scaled_offset << 10) | register_field(base, 5) |
                          destination);
    }

    void
    AArch64Assembler::emit_b_conditional_immediate(AArch64Condition condition,
                                                   int32_t byte_displacement)
    {
        uint32_t immediate = signed_immediate(byte_displacement, 19, 2);
        write_instruction(0x54000000 | (immediate << 5) |
                          static_cast<uint32_t>(condition));
    }

    void AArch64Assembler::emit_b_immediate_26(int64_t byte_displacement)
    {
        write_instruction(0x14000000 |
                          signed_immediate(byte_displacement, 26, 2));
    }

    void AArch64Assembler::emit_bl_immediate_26(int64_t byte_displacement)
    {
        write_instruction(0x94000000 |
                          signed_immediate(byte_displacement, 26, 2));
    }

    void AArch64Assembler::emit_br(XRegister target)
    {
        write_instruction(0xd61f0000 | register_field(target.encoding(), 5));
    }

    void AArch64Assembler::emit_blr(XRegister target)
    {
        write_instruction(0xd63f0000 | register_field(target.encoding(), 5));
    }

    void AArch64Assembler::emit_ret(XRegister target)
    {
        write_instruction(0xd65f0000 | register_field(target.encoding(), 5));
    }

    void
    AArch64Assembler::emit_ldr_literal_immediate_19(XRegister destination,
                                                    int64_t byte_displacement)
    {
        write_instruction(0x58000000 |
                          (signed_immediate(byte_displacement, 19, 2) << 5) |
                          destination.encoding());
    }

    void
    AArch64Assembler::emit_adrp_page_immediate_21(XRegister destination,
                                                  int64_t page_displacement)
    {
        assert(page_displacement % 4096 == 0);
        uint32_t immediate = signed_immediate(page_displacement, 21, 12);
        uint32_t immediate_low = immediate & 3;
        uint32_t immediate_high = immediate >> 2;
        write_instruction(0x90000000 | (immediate_low << 29) |
                          (immediate_high << 5) | destination.encoding());
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
