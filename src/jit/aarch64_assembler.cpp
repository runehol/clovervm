#include "jit/aarch64_assembler.h"

#include "jit/transition_program.h"
#include "runtime/fatal.h"

#include <cassert>
#include <cstring>
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

        int64_t decode_signed_scaled_immediate(uint32_t immediate,
                                               uint8_t immediate_bits,
                                               uint8_t scale_shift)
        {
            assert(immediate_bits != 0);
            assert(immediate < (uint32_t{1} << immediate_bits));
            int64_t value = immediate;
            if((immediate & (uint32_t{1} << (immediate_bits - 1))) != 0)
            {
                value -= int64_t{1} << immediate_bits;
            }
            return value * (int64_t{1} << scale_shift);
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

        void emit_load_store(AArch64EmitterAssembler &assembler,
                             LoadStoreOp operation, XRegister value,
                             XRegisterOrSP base, int64_t byte_offset)
        {
            constexpr int64_t MaximumScaledByteOffset = 4095 * 8;
            if(byte_offset >= 0 && byte_offset % 8 == 0 &&
               byte_offset <= MaximumScaledByteOffset)
            {
                assembler.emit_load_store_unsigned_offset(
                    operation, value, base, static_cast<uint16_t>(byte_offset));
                return;
            }
            if(aarch64_detail::fits_signed_scaled_displacement(byte_offset, 9,
                                                               0))
            {
                assembler.emit_load_store_unscaled(
                    operation, value, base, static_cast<int16_t>(byte_offset));
                return;
            }
            fatal("AArch64 load/store offset is not encodable");
        }
    }  // namespace

    uint32_t AArch64Relaxation::select(MachineAddress source,
                                       MachineAddress target)
    {
        assert(mode_ == UnselectedMode);
        int64_t displacement = source.displacement_to(target);
        switch(kind_)
        {
            case AArch64RelaxationKind::UnconditionalBranch:
            case AArch64RelaxationKind::Call:
                if(aarch64_detail::fits_signed_scaled_displacement(displacement,
                                                                   26, 2))
                {
                    mode_ = NearMode;
                    return 4;
                }
                mode_ = FarMode;
                return 20;
            case AArch64RelaxationKind::ConditionalBranch:
                if(aarch64_detail::fits_signed_scaled_displacement(displacement,
                                                                   19, 2))
                {
                    mode_ = NearMode;
                    return 4;
                }
                assert(aarch64_detail::fits_signed_scaled_displacement(
                    source.offset_by(4).displacement_to(target), 26, 2));
                mode_ = FarMode;
                return 8;
        }
        assert(false);
        return 0;
    }

    void AArch64Relaxation::encode(void *write_pointer, MachineAddress source,
                                   MachineAddress target) const
    {
        assert(mode_ != UnselectedMode);
        AArch64BufferAssembler assembler(write_pointer);
        int64_t displacement = source.displacement_to(target);
        switch(mode_)
        {
            case UnselectedMode:
                assert(false);
                return;
            case NearMode:
                switch(kind_)
                {
                    case AArch64RelaxationKind::UnconditionalBranch:
                        assembler.emit_b_immediate_26(displacement);
                        return;
                    case AArch64RelaxationKind::Call:
                        assembler.emit_bl_immediate_26(displacement);
                        return;
                    case AArch64RelaxationKind::ConditionalBranch:
                        assembler.emit_b_conditional_immediate(
                            static_cast<AArch64Condition>(kind_data_),
                            displacement);
                        return;
                }
            case FarMode:
                break;
            default:
                assert(false);
                return;
        }

        switch(kind_)
        {
            case AArch64RelaxationKind::UnconditionalBranch:
            case AArch64RelaxationKind::Call:
                break;
            case AArch64RelaxationKind::ConditionalBranch:
                {
                    auto condition = static_cast<AArch64Condition>(kind_data_);
                    auto inverse = static_cast<AArch64Condition>(
                        static_cast<uint8_t>(condition) ^ 1);
                    assembler.emit_b_conditional_immediate(inverse, 8);
                    assembler.emit_b_immediate_26(
                        source.offset_by(4).displacement_to(target));
                    return;
                }
        }

        XRegister scratch(kind_data_);
        uintptr_t address = target.bits_for_indirect_target();
        assembler.emit_move_wide_imm16(MoveWideOp::Movz, scratch,
                                       static_cast<uint16_t>(address),
                                       MoveWideHalfword::Bits0);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, scratch,
                                       static_cast<uint16_t>(address >> 16),
                                       MoveWideHalfword::Bits16);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, scratch,
                                       static_cast<uint16_t>(address >> 32),
                                       MoveWideHalfword::Bits32);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, scratch,
                                       static_cast<uint16_t>(address >> 48),
                                       MoveWideHalfword::Bits48);
        switch(kind_)
        {
            case AArch64RelaxationKind::UnconditionalBranch:
                assembler.emit_br(scratch);
                return;
            case AArch64RelaxationKind::Call:
                assembler.emit_blr(scratch);
                return;
            case AArch64RelaxationKind::ConditionalBranch:
                assert(false);
                return;
        }
    }

    void AArch64Relocation::apply(void *write_pointer,
                                  MachineAddress instruction_pc,
                                  MachineAddress target) const
    {
        uint32_t instruction;
        std::memcpy(&instruction, write_pointer, sizeof(instruction));

        switch(kind_)
        {
            case AArch64RelocationKind::PcRelative19Scaled4:
                {
                    constexpr uint32_t ImmediateMask = 0x7ffff << 5;
                    int64_t addend = decode_signed_scaled_immediate(
                        (instruction & ImmediateMask) >> 5, 19, 2);
                    uint32_t immediate = aarch64_detail::signed_immediate(
                        addend + instruction_pc.displacement_to(target), 19, 2);
                    instruction =
                        (instruction & ~ImmediateMask) | (immediate << 5);
                    break;
                }
            case AArch64RelocationKind::PcRelative21:
                {
                    constexpr uint32_t ImmediateMask =
                        (uint32_t{3} << 29) | (uint32_t{0x7ffff} << 5);
                    uint32_t encoded_addend =
                        ((instruction >> 29) & 3) |
                        (((instruction >> 5) & 0x7ffff) << 2);
                    int64_t addend =
                        decode_signed_scaled_immediate(encoded_addend, 21, 0);
                    int64_t displacement =
                        addend + instruction_pc.displacement_to(target);
                    uint32_t immediate =
                        aarch64_detail::signed_immediate(displacement, 21, 0);
                    instruction = (instruction & ~ImmediateMask) |
                                  ((immediate & 3) << 29) |
                                  ((immediate >> 2) << 5);
                    break;
                }
            case AArch64RelocationKind::PageRelative21:
                {
                    constexpr uint32_t ImmediateMask =
                        (uint32_t{3} << 29) | (uint32_t{0x7ffff} << 5);
                    uint32_t encoded_addend =
                        ((instruction >> 29) & 3) |
                        (((instruction >> 5) & 0x7ffff) << 2);
                    int64_t addend =
                        decode_signed_scaled_immediate(encoded_addend, 21, 12);
                    int64_t page_displacement =
                        addend +
                        instruction_pc.aligned_displacement_to(target, 12);
                    uint32_t immediate = aarch64_detail::signed_immediate(
                        page_displacement, 21, 12);
                    instruction = (instruction & ~ImmediateMask) |
                                  ((immediate & 3) << 29) |
                                  ((immediate >> 2) << 5);
                    break;
                }
            case AArch64RelocationKind::PageOffset12:
                {
                    constexpr uint32_t ImmediateMask = 0xfff << 10;
                    uint32_t addend = (instruction & ImmediateMask) >> 10;
                    uintptr_t byte_offset = addend + target.offset_within(12);
                    assert(byte_offset < (1 << 12));
                    instruction = (instruction & ~ImmediateMask) |
                                  (static_cast<uint32_t>(byte_offset) << 10);
                    break;
                }
            case AArch64RelocationKind::PageOffset12Scaled8:
                {
                    constexpr uint32_t ImmediateMask = 0xfff << 10;
                    uint32_t addend = ((instruction & ImmediateMask) >> 10) *
                                      sizeof(uint64_t);
                    uintptr_t byte_offset = addend + target.offset_within(12);
                    assert(byte_offset % sizeof(uint64_t) == 0);
                    uint32_t scaled_offset =
                        static_cast<uint32_t>(byte_offset / sizeof(uint64_t));
                    assert(scaled_offset < (1 << 12));
                    instruction =
                        (instruction & ~ImmediateMask) | (scaled_offset << 10);
                    break;
                }
        }
        std::memcpy(write_pointer, &instruction, sizeof(instruction));
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

    void AArch64MacroAssembler::madd(XRegisterOrZero destination,
                                     XRegisterOrZero multiplicand1,
                                     XRegisterOrZero multiplicand2,
                                     XRegisterOrZero addend)
    {
        emit_multiply_add(destination, multiplicand1, multiplicand2, addend);
    }

    void AArch64MacroAssembler::mul(XRegisterOrZero destination,
                                    XRegisterOrZero multiplicand1,
                                    XRegisterOrZero multiplicand2)
    {
        emit_multiply_add(destination, multiplicand1, multiplicand2, xzr);
    }

    void AArch64MacroAssembler::smulh(XRegisterOrZero destination,
                                      XRegisterOrZero multiplicand1,
                                      XRegisterOrZero multiplicand2)
    {
        emit_multiply_high(MultiplyHighOp::Smulh, destination, multiplicand1,
                           multiplicand2);
    }

    void AArch64MacroAssembler::umulh(XRegisterOrZero destination,
                                      XRegisterOrZero multiplicand1,
                                      XRegisterOrZero multiplicand2)
    {
        emit_multiply_high(MultiplyHighOp::Umulh, destination, multiplicand1,
                           multiplicand2);
    }

    void AArch64MacroAssembler::cmp(XRegisterOrZero left, XRegisterOrZero right)
    {
        emit_arithmetic_reg(ArithmeticOp::Subs, xzr, left, right);
    }

    void AArch64MacroAssembler::cmp(XRegister left, uint16_t immediate)
    {
        emit_arithmetic_imm12(ArithmeticOp::Subs, xzr, left, immediate);
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

    void AArch64MacroAssembler::tst(XRegisterOrZero left, XRegisterOrZero right)
    {
        emit_logical_reg(LogicalOp::Ands, xzr, left, right);
    }

    void AArch64MacroAssembler::tst(XRegisterOrZero source, uint64_t immediate)
    {
        emit_logical_imm(LogicalOp::Ands, xzr, source, immediate);
    }

    void AArch64MacroAssembler::ldr(XRegister destination, XRegisterOrSP base,
                                    int64_t byte_offset)
    {
        emit_load_store(*this, LoadStoreOp::Load, destination, base,
                        byte_offset);
    }

    void AArch64MacroAssembler::ldr(XRegister destination, Value value)
    {
        ConstantPoolEntry entry = emitter().add_value_to_constant_pool(value);
        if(pool_mode_ == AArch64ValuePoolMode::NearLiteral)
        {
            emit_ldr_literal_immediate_19(destination, 0);
            emitter().add_relocation_to_last_emitted(
                sizeof(uint32_t),
                AArch64Relocation(entry,
                                  AArch64RelocationKind::PcRelative19Scaled4));
            return;
        }

        emit_adrp_page_immediate_21(destination, 0);
        emitter().add_relocation_to_last_emitted(
            sizeof(uint32_t),
            AArch64Relocation(entry, AArch64RelocationKind::PageRelative21));
        emit_load_store_unsigned_offset(LoadStoreOp::Load, destination,
                                        destination, 0);
        emitter().add_relocation_to_last_emitted(
            sizeof(uint32_t),
            AArch64Relocation(entry,
                              AArch64RelocationKind::PageOffset12Scaled8));
    }

    void AArch64MacroAssembler::adr(XRegister destination,
                                    ConstantPoolEntry target)
    {
        if(pool_mode_ == AArch64ValuePoolMode::NearLiteral)
        {
            emit_adr_immediate_21(destination, 0);
            emitter().add_relocation_to_last_emitted(
                sizeof(uint32_t),
                AArch64Relocation(target, AArch64RelocationKind::PcRelative21));
            return;
        }

        emit_adrp_page_immediate_21(destination, 0);
        emitter().add_relocation_to_last_emitted(
            sizeof(uint32_t),
            AArch64Relocation(target, AArch64RelocationKind::PageRelative21));
        emit_arithmetic_imm12(ArithmeticOp::Add, destination, destination, 0);
        emitter().add_relocation_to_last_emitted(
            sizeof(uint32_t),
            AArch64Relocation(target, AArch64RelocationKind::PageOffset12));
    }

    ConstantPoolEntry AArch64MacroAssembler::add_transition_program(
        std::span<const TransitionInstruction> program)
    {
        assert(!program.empty());
        return emitter().add_data_to_constant_pool(std::as_bytes(program));
    }

    void AArch64MacroAssembler::str(XRegister source, XRegisterOrSP base,
                                    int64_t byte_offset)
    {
        emit_load_store(*this, LoadStoreOp::Store, source, base, byte_offset);
    }

    void AArch64MacroAssembler::b(CodeTarget target, XRegister scratch)
    {
        emitter().emit_relaxation(
            AArch64Relaxation::unconditional_branch(target, scratch));
    }

    void AArch64MacroAssembler::b(AArch64Condition condition, Label target)
    {
        emitter().emit_relaxation(
            AArch64Relaxation::conditional_branch(target, condition));
    }

    void AArch64MacroAssembler::bl(CodeTarget target, XRegister scratch)
    {
        emitter().emit_relaxation(AArch64Relaxation::call(target, scratch));
    }

}  // namespace cl::jit
