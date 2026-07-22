#ifndef CL_JIT_AARCH64_ASSEMBLER_H
#define CL_JIT_AARCH64_ASSEMBLER_H

#include "jit/machine_code_emitter.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace cl::jit
{
    enum class GPRWidth : uint32_t
    {
        W = 0,
        X = 1u << 31,
    };

    template <GPRWidth Width> class GPRRegister
    {
    public:
        explicit constexpr GPRRegister(uint32_t register_number)
            : register_number_(register_number)
        {
            assert(register_number < 31);
        }

        constexpr uint32_t encoding() const { return register_number_; }

    private:
        uint32_t register_number_;
    };

    template <GPRWidth Width> struct GPRSP
    {
    };
    template <GPRWidth Width> struct GPRZero
    {
    };

    template <GPRWidth Width> class GPRRegisterOrSP
    {
    public:
        constexpr GPRRegisterOrSP(GPRRegister<Width> reg)
            : register_number_(reg.encoding())
        {
        }
        constexpr GPRRegisterOrSP(GPRSP<Width>) : register_number_(31) {}

        constexpr uint32_t encoding() const { return register_number_; }

    private:
        uint32_t register_number_;
    };

    template <GPRWidth Width> class GPRRegisterOrZero
    {
    public:
        constexpr GPRRegisterOrZero(GPRRegister<Width> reg)
            : register_number_(reg.encoding())
        {
        }
        constexpr GPRRegisterOrZero(GPRZero<Width>) : register_number_(31) {}

        constexpr uint32_t encoding() const { return register_number_; }

    private:
        uint32_t register_number_;
    };

    template <GPRWidth Width> class GPRAddSubDestination
    {
    public:
        constexpr GPRAddSubDestination(GPRRegister<Width> reg)
            : register_number_(reg.encoding()), zero_(false)
        {
        }
        constexpr GPRAddSubDestination(GPRSP<Width>)
            : register_number_(31), zero_(false)
        {
        }
        constexpr GPRAddSubDestination(GPRZero<Width>)
            : register_number_(31), zero_(true)
        {
        }

        constexpr uint32_t encoding() const { return register_number_; }
        constexpr bool is_zero() const { return zero_; }

    private:
        uint32_t register_number_;
        bool zero_;
    };

    using XRegister = GPRRegister<GPRWidth::X>;
    using WRegister = GPRRegister<GPRWidth::W>;
    using XSP = GPRSP<GPRWidth::X>;
    using WSP = GPRSP<GPRWidth::W>;
    using XZero = GPRZero<GPRWidth::X>;
    using WZero = GPRZero<GPRWidth::W>;
    using XRegisterOrSP = GPRRegisterOrSP<GPRWidth::X>;
    using WRegisterOrSP = GPRRegisterOrSP<GPRWidth::W>;
    using XRegisterOrZero = GPRRegisterOrZero<GPRWidth::X>;
    using WRegisterOrZero = GPRRegisterOrZero<GPRWidth::W>;
    using XAddSubDestination = GPRAddSubDestination<GPRWidth::X>;
    using WAddSubDestination = GPRAddSubDestination<GPRWidth::W>;

    inline constexpr XSP xsp;
    inline constexpr WSP wsp;
    inline constexpr XZero xzr;
    inline constexpr WZero wzr;

    // Keep future instruction families aligned with the architectural encoding
    // patterns documented here:
    // https://developer.arm.com/documentation/ddi0602/2026-06/Index-by-Encoding
    // Prefer one method per encoding class, with typed enums containing their
    // field bits in place, over one method per instruction mnemonic.
    enum class ArithmeticOp : uint32_t
    {
        Add = 0u << 29,
        Adds = 1u << 29,
        Sub = 2u << 29,
        Subs = 3u << 29,
    };

    enum class LogicalOp : uint32_t
    {
        And = 0u << 29,
        Orr = 1u << 29,
        Eor = 2u << 29,
        Ands = 3u << 29,
    };

    enum class InvertMode : uint32_t
    {
        Normal = 0,
        Invert = 1u << 21,
    };

    enum class ArithmeticShift : uint32_t
    {
        Lsl = 0u << 22,
        Lsr = 1u << 22,
        Asr = 2u << 22,
    };

    enum class LogicalShift : uint32_t
    {
        Lsl = 0u << 22,
        Lsr = 1u << 22,
        Asr = 2u << 22,
        Ror = 3u << 22,
    };

    enum class AddImmediateShift : uint32_t
    {
        None = 0,
        Twelve = 1u << 22,
    };

    enum class MoveWideOp : uint32_t
    {
        Movn = 0u << 29,
        Movz = 2u << 29,
        Movk = 3u << 29,
    };

    enum class MoveWideHalfword : uint32_t
    {
        Bits0 = 0u << 21,
        Bits16 = 1u << 21,
        Bits32 = 2u << 21,
        Bits48 = 3u << 21,
    };

    enum class AArch64Condition : uint8_t
    {
        Equal = 0,
        NotEqual = 1,
        CarrySet = 2,
        CarryClear = 3,
        Negative = 4,
        PositiveOrZero = 5,
        Overflow = 6,
        NoOverflow = 7,
        UnsignedHigher = 8,
        UnsignedLowerOrSame = 9,
        SignedGreaterOrEqual = 10,
        SignedLess = 11,
        SignedGreater = 12,
        SignedLessOrEqual = 13,
    };

    enum class AArch64ValuePoolMode : uint8_t
    {
        NearLiteral,
        FarPageRelative,
    };

    enum class AArch64BranchKind : uint8_t
    {
        Jump,
        Call,
    };

    namespace aarch64_detail
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

        inline bool fits_signed_scaled_displacement(int64_t displacement,
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

        inline uint32_t signed_immediate(int64_t displacement,
                                         uint8_t immediate_bits,
                                         uint8_t scale_shift)
        {
            assert(fits_signed_scaled_displacement(displacement, immediate_bits,
                                                   scale_shift));
            uint64_t mask = (uint64_t{1} << immediate_bits) - 1;
            return static_cast<uint32_t>(
                (static_cast<uint64_t>(displacement >> scale_shift)) & mask);
        }
    }  // namespace aarch64_detail

    class AArch64DirectBranch
    {
    public:
        static constexpr size_t MaximumUnitSize = 128 * 1024 * 1024;

        AArch64DirectBranch(CodeTarget target, AArch64BranchKind kind,
                            XRegister scratch)
            : target_(target), scratch_(scratch), kind_(kind)
        {
        }

        const CodeTarget &target() const { return target_; }
        uint32_t min_size() const { return 4; }
        uint32_t max_size() const { return 20; }

        uint32_t select(MachineAddress source, MachineAddress target);
        void encode(void *write_pointer, MachineAddress source,
                    MachineAddress target) const;

    private:
        CodeTarget target_;
        XRegister scratch_;
        AArch64BranchKind kind_;
        std::optional<bool> direct_;
    };

    class AArch64Relocation
    {
    public:
        AArch64Relocation(ValuePoolEntry target, XRegister destination,
                          AArch64ValuePoolMode mode)
            : target_(target), destination_(destination), mode_(mode)
        {
        }

        RelocationTarget target() const { return target_; }
        void apply(void *write_pointer, MachineAddress instruction_pc,
                   MachineAddress target) const;

    private:
        ValuePoolEntry target_;
        XRegister destination_;
        AArch64ValuePoolMode mode_;
    };

    using AArch64Emitter =
        MachineCodeEmitter<AArch64DirectBranch, AArch64Relocation>;

    class AArch64EmitterSink
    {
    public:
        explicit AArch64EmitterSink(size_t maximum_pool_span)
            : emitter_(maximum_pool_span)
        {
        }

        void write(uint32_t instruction)
        {
            emitter_.emit_bytes(&instruction, sizeof(instruction));
        }

        AArch64Emitter &emitter() { return emitter_; }

    private:
        AArch64Emitter emitter_;
    };

    class AArch64BufferSink
    {
    public:
        explicit AArch64BufferSink(void *output)
            : output_(static_cast<uint8_t *>(output))
        {
            assert(output != nullptr);
        }

        void write(uint32_t instruction)
        {
            std::memcpy(output_, &instruction, sizeof(instruction));
            output_ += sizeof(instruction);
        }

    private:
        uint8_t *output_;
    };

    template <typename Sink> class AArch64Assembler
    {
    public:
        explicit AArch64Assembler(size_t maximum_pool_span)
            : sink_(maximum_pool_span)
        {
        }
        explicit AArch64Assembler(void *output) : sink_(output) {}

        AArch64Emitter &emitter() { return sink_.emitter(); }

        void
        emit_arithmetic_imm12(ArithmeticOp operation,
                              XAddSubDestination destination,
                              XRegisterOrSP source, uint16_t immediate,
                              AddImmediateShift shift = AddImmediateShift::None)
        {
            assert(immediate < (1 << 12));
            if(destination.encoding() == 31)
            {
                bool sets_flags =
                    (static_cast<uint32_t>(operation) & (1u << 29)) != 0;
                (void)sets_flags;
                assert(destination.is_zero() == sets_flags);
            }
            emit_arithmetic_imm12(GPRWidth::X, operation,
                                  destination.encoding(), source.encoding(),
                                  immediate, shift);
        }

        void
        emit_arithmetic_imm12(ArithmeticOp operation,
                              WAddSubDestination destination,
                              WRegisterOrSP source, uint16_t immediate,
                              AddImmediateShift shift = AddImmediateShift::None)
        {
            assert(immediate < (1 << 12));
            if(destination.encoding() == 31)
            {
                bool sets_flags =
                    (static_cast<uint32_t>(operation) & (1u << 29)) != 0;
                (void)sets_flags;
                assert(destination.is_zero() == sets_flags);
            }
            emit_arithmetic_imm12(GPRWidth::W, operation,
                                  destination.encoding(), source.encoding(),
                                  immediate, shift);
        }

        void emit_arithmetic_reg(ArithmeticOp operation,
                                 XRegisterOrZero destination,
                                 XRegisterOrZero source1,
                                 XRegisterOrZero source2,
                                 ArithmeticShift shift = ArithmeticShift::Lsl,
                                 uint8_t shift_amount = 0)
        {
            assert(shift_amount < 64);
            emit_arithmetic_reg(GPRWidth::X, operation, destination.encoding(),
                                source1.encoding(), source2.encoding(), shift,
                                shift_amount);
        }

        void emit_arithmetic_reg(ArithmeticOp operation,
                                 WRegisterOrZero destination,
                                 WRegisterOrZero source1,
                                 WRegisterOrZero source2,
                                 ArithmeticShift shift = ArithmeticShift::Lsl,
                                 uint8_t shift_amount = 0)
        {
            assert(shift_amount < 32);
            emit_arithmetic_reg(GPRWidth::W, operation, destination.encoding(),
                                source1.encoding(), source2.encoding(), shift,
                                shift_amount);
        }

        void emit_logical_reg(LogicalOp operation, XRegisterOrZero destination,
                              XRegisterOrZero source1, XRegisterOrZero source2,
                              InvertMode invert = InvertMode::Normal,
                              LogicalShift shift = LogicalShift::Lsl,
                              uint8_t shift_amount = 0)
        {
            assert(shift_amount < 64);
            emit_logical_reg(GPRWidth::X, operation, destination.encoding(),
                             source1.encoding(), source2.encoding(), invert,
                             shift, shift_amount);
        }

        void emit_logical_reg(LogicalOp operation, WRegisterOrZero destination,
                              WRegisterOrZero source1, WRegisterOrZero source2,
                              InvertMode invert = InvertMode::Normal,
                              LogicalShift shift = LogicalShift::Lsl,
                              uint8_t shift_amount = 0)
        {
            assert(shift_amount < 32);
            emit_logical_reg(GPRWidth::W, operation, destination.encoding(),
                             source1.encoding(), source2.encoding(), invert,
                             shift, shift_amount);
        }

        void emit_move_wide_imm16(
            MoveWideOp operation, XRegisterOrZero destination,
            uint16_t immediate,
            MoveWideHalfword halfword = MoveWideHalfword::Bits0)
        {
            emit_move_wide_imm16(GPRWidth::X, operation, destination.encoding(),
                                 immediate, halfword);
        }

        void emit_move_wide_imm16(
            MoveWideOp operation, WRegisterOrZero destination,
            uint16_t immediate,
            MoveWideHalfword halfword = MoveWideHalfword::Bits0)
        {
            assert(halfword == MoveWideHalfword::Bits0 ||
                   halfword == MoveWideHalfword::Bits16);
            emit_move_wide_imm16(GPRWidth::W, operation, destination.encoding(),
                                 immediate, halfword);
        }

        void emit_ldr_unsigned_offset(XRegisterOrZero destination,
                                      XRegisterOrSP base, uint16_t byte_offset)
        {
            assert(byte_offset % 8 == 0);
            uint32_t scaled_offset = byte_offset / 8;
            assert(scaled_offset < (1 << 12));
            emit_ldr_unsigned_offset(GPRWidth::X, destination.encoding(),
                                     base.encoding(), scaled_offset);
        }

        void emit_ldr_unsigned_offset(WRegisterOrZero destination,
                                      XRegisterOrSP base, uint16_t byte_offset)
        {
            assert(byte_offset % 4 == 0);
            uint32_t scaled_offset = byte_offset / 4;
            assert(scaled_offset < (1 << 12));
            emit_ldr_unsigned_offset(GPRWidth::W, destination.encoding(),
                                     base.encoding(), scaled_offset);
        }

        void emit_b_conditional_immediate(AArch64Condition condition,
                                          int32_t byte_displacement)
        {
            uint32_t immediate =
                aarch64_detail::signed_immediate(byte_displacement, 19, 2);
            write_instruction(0x54000000 | (immediate << 5) |
                              static_cast<uint32_t>(condition));
        }

        void emit_b_immediate_26(int64_t byte_displacement)
        {
            write_instruction(0x14000000 | aarch64_detail::signed_immediate(
                                               byte_displacement, 26, 2));
        }

        void emit_bl_immediate_26(int64_t byte_displacement)
        {
            write_instruction(0x94000000 | aarch64_detail::signed_immediate(
                                               byte_displacement, 26, 2));
        }

        void emit_br(XRegister target)
        {
            write_instruction(0xd61f0000 | aarch64_detail::register_field(
                                               target.encoding(), 5));
        }

        void emit_blr(XRegister target)
        {
            write_instruction(0xd63f0000 | aarch64_detail::register_field(
                                               target.encoding(), 5));
        }

        void emit_ret(XRegister target = XRegister(30))
        {
            write_instruction(0xd65f0000 | aarch64_detail::register_field(
                                               target.encoding(), 5));
        }

        void emit_ldr_literal_immediate_19(XRegister destination,
                                           int64_t byte_displacement)
        {
            write_instruction(
                0x58000000 |
                (aarch64_detail::signed_immediate(byte_displacement, 19, 2)
                 << 5) |
                destination.encoding());
        }

        void emit_adrp_page_immediate_21(XRegister destination,
                                         int64_t page_displacement)
        {
            assert(page_displacement % 4096 == 0);
            uint32_t immediate =
                aarch64_detail::signed_immediate(page_displacement, 21, 12);
            uint32_t immediate_low = immediate & 3;
            uint32_t immediate_high = immediate >> 2;
            write_instruction(0x90000000 | (immediate_low << 29) |
                              (immediate_high << 5) | destination.encoding());
        }

    private:
        void emit_arithmetic_imm12(GPRWidth width, ArithmeticOp operation,
                                   uint32_t destination, uint32_t source,
                                   uint16_t immediate, AddImmediateShift shift)
        {
            write_instruction(
                0x11000000 | aarch64_detail::encoding_bits(width) |
                aarch64_detail::encoding_bits(operation) |
                aarch64_detail::encoding_bits(shift) |
                (static_cast<uint32_t>(immediate) << 10) |
                aarch64_detail::register_field(source, 5) | destination);
        }

        void emit_arithmetic_reg(GPRWidth width, ArithmeticOp operation,
                                 uint32_t destination, uint32_t source1,
                                 uint32_t source2, ArithmeticShift shift,
                                 uint8_t shift_amount)
        {
            write_instruction(
                0x0b000000 | aarch64_detail::encoding_bits(width) |
                aarch64_detail::encoding_bits(operation) |
                aarch64_detail::encoding_bits(shift) |
                aarch64_detail::register_field(source2, 16) |
                (static_cast<uint32_t>(shift_amount) << 10) |
                aarch64_detail::register_field(source1, 5) | destination);
        }

        void emit_logical_reg(GPRWidth width, LogicalOp operation,
                              uint32_t destination, uint32_t source1,
                              uint32_t source2, InvertMode invert,
                              LogicalShift shift, uint8_t shift_amount)
        {
            write_instruction(
                0x0a000000 | aarch64_detail::encoding_bits(width) |
                aarch64_detail::encoding_bits(operation) |
                aarch64_detail::encoding_bits(invert) |
                aarch64_detail::encoding_bits(shift) |
                aarch64_detail::register_field(source2, 16) |
                (static_cast<uint32_t>(shift_amount) << 10) |
                aarch64_detail::register_field(source1, 5) | destination);
        }

        void emit_move_wide_imm16(GPRWidth width, MoveWideOp operation,
                                  uint32_t destination, uint16_t immediate,
                                  MoveWideHalfword halfword)
        {
            write_instruction(
                0x12800000 | aarch64_detail::encoding_bits(width) |
                aarch64_detail::encoding_bits(operation) |
                aarch64_detail::encoding_bits(halfword) |
                (static_cast<uint32_t>(immediate) << 5) | destination);
        }

        void emit_ldr_unsigned_offset(GPRWidth width, uint32_t destination,
                                      uint32_t base, uint32_t scaled_offset)
        {
            write_instruction(
                0xb9400000 | (aarch64_detail::encoding_bits(width) >> 1) |
                (scaled_offset << 10) |
                aarch64_detail::register_field(base, 5) | destination);
        }

        void write_instruction(uint32_t instruction)
        {
            sink_.write(instruction);
        }

        Sink sink_;
    };

    using AArch64EmitterAssembler = AArch64Assembler<AArch64EmitterSink>;
    using AArch64BufferAssembler = AArch64Assembler<AArch64BufferSink>;

    class AArch64MacroAssembler : public AArch64EmitterAssembler
    {
    public:
        explicit AArch64MacroAssembler(AArch64ValuePoolMode pool_mode)
            : AArch64EmitterAssembler(maximum_pool_span(pool_mode)),
              pool_mode_(pool_mode)
        {
        }

        void mov(XRegisterOrZero destination, uint64_t immediate);
        void mov(WRegisterOrZero destination, uint32_t immediate);
        void mov(XRegisterOrZero destination, XRegisterOrZero source);
        void mov(WRegisterOrZero destination, WRegisterOrZero source);
        void mvn(XRegisterOrZero destination, XRegisterOrZero source);
        void mvn(WRegisterOrZero destination, WRegisterOrZero source);
        void neg(XRegisterOrZero destination, XRegisterOrZero source);
        void neg(WRegisterOrZero destination, WRegisterOrZero source);
        void cmp(XRegisterOrZero left, XRegisterOrZero right);
        void cmp(WRegisterOrZero left, WRegisterOrZero right);
        void cmn(XRegisterOrZero left, XRegisterOrZero right);
        void cmn(WRegisterOrZero left, WRegisterOrZero right);
        void ldr(XRegister destination, Value value);

        void b(CodeTarget target, XRegister scratch = XRegister(16));
        void bl(CodeTarget target, XRegister scratch = XRegister(16));

    private:
        static constexpr size_t
        maximum_pool_span(AArch64ValuePoolMode pool_mode)
        {
            return pool_mode == AArch64ValuePoolMode::NearLiteral
                       ? 1024 * 1024
                       : AArch64DirectBranch::MaximumUnitSize;
        }

        AArch64ValuePoolMode pool_mode_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_ASSEMBLER_H
