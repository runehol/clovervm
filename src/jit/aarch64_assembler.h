#ifndef CL_JIT_AARCH64_ASSEMBLER_H
#define CL_JIT_AARCH64_ASSEMBLER_H

#include "jit/machine_code_emitter.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace cl::jit
{
    class TransitionInstruction;

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

    enum class SIMDElementWidth : uint16_t
    {
        Bits8 = 8,
        Bits16 = 16,
        Bits32 = 32,
        Bits64 = 64,
    };

    enum class SIMDRegisterWidth : uint16_t
    {
        Bits64 = 64,
        Bits128 = 128,
    };

    template <SIMDElementWidth ElementWidth> class SIMDScalarRegister
    {
    public:
        explicit constexpr SIMDScalarRegister(uint32_t register_number)
            : register_number_(register_number)
        {
            assert(register_number < 32);
        }

        constexpr uint32_t encoding() const { return register_number_; }

    private:
        uint32_t register_number_;
    };

    template <SIMDRegisterWidth RegisterWidth, SIMDElementWidth ElementWidth>
    class SIMDVectorRegister
    {
        static_assert(static_cast<uint16_t>(RegisterWidth) %
                              static_cast<uint16_t>(ElementWidth) ==
                          0,
                      "SIMD vector element width does not divide its register "
                      "width");

    public:
        explicit constexpr SIMDVectorRegister(uint32_t register_number)
            : register_number_(register_number)
        {
            assert(register_number < 32);
        }

        constexpr uint32_t encoding() const { return register_number_; }

    private:
        uint32_t register_number_;
    };

    using BRegister = SIMDScalarRegister<SIMDElementWidth::Bits8>;
    using HRegister = SIMDScalarRegister<SIMDElementWidth::Bits16>;
    using SRegister = SIMDScalarRegister<SIMDElementWidth::Bits32>;
    using DRegister = SIMDScalarRegister<SIMDElementWidth::Bits64>;

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

    enum class FPBinaryOp : uint32_t
    {
        Mul = 0u << 12,
        Div = 1u << 12,
        Add = 2u << 12,
        Sub = 3u << 12,
        Max = 4u << 12,
        Min = 5u << 12,
        MaxNumber = 6u << 12,
        MinNumber = 7u << 12,
        NegatedMul = 8u << 12,
    };

    enum class FPUnaryOp : uint32_t
    {
        Mov = 0u << 15,
        Abs = 1u << 15,
        Neg = 2u << 15,
        Sqrt = 3u << 15,
        RoundNearestEven = 8u << 15,
        RoundPlusInfinity = 9u << 15,
        RoundMinusInfinity = 10u << 15,
        RoundTowardZero = 11u << 15,
        RoundNearestAway = 12u << 15,
        RoundCurrentExact = 14u << 15,
        RoundCurrent = 15u << 15,
    };

    enum class FPCompareMode : uint32_t
    {
        Quiet = 0,
        Signaling = 1u << 4,
    };

    enum class LogicalOp : uint32_t
    {
        And = 0u << 29,
        Orr = 1u << 29,
        Eor = 2u << 29,
        Ands = 3u << 29,
    };

    enum class MultiplyHighOp : uint32_t
    {
        Smulh = 0,
        Umulh = 1u << 23,
    };

    enum class LoadStoreOp : uint32_t
    {
        Store = 0,
        Load = 1u << 22,
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

    constexpr AArch64Condition invert_condition(AArch64Condition condition)
    {
        return static_cast<AArch64Condition>(static_cast<uint8_t>(condition) ^
                                             1u);
    }

    static_assert(invert_condition(AArch64Condition::Equal) ==
                  AArch64Condition::NotEqual);
    static_assert(invert_condition(AArch64Condition::CarrySet) ==
                  AArch64Condition::CarryClear);
    static_assert(invert_condition(AArch64Condition::Negative) ==
                  AArch64Condition::PositiveOrZero);
    static_assert(invert_condition(AArch64Condition::Overflow) ==
                  AArch64Condition::NoOverflow);
    static_assert(invert_condition(AArch64Condition::UnsignedHigher) ==
                  AArch64Condition::UnsignedLowerOrSame);
    static_assert(invert_condition(AArch64Condition::SignedGreaterOrEqual) ==
                  AArch64Condition::SignedLess);
    static_assert(invert_condition(AArch64Condition::SignedGreater) ==
                  AArch64Condition::SignedLessOrEqual);

    enum class AArch64ValuePoolMode : uint8_t
    {
        NearLiteral,
        FarPageRelative,
    };

    enum class AArch64RelaxationKind : uint8_t
    {
        UnconditionalBranch,
        Call,
        ConditionalBranch,
    };

    namespace aarch64_detail
    {
        enum class LoadStoreAddressMode : uint8_t
        {
            UnsignedScaled,
            SignedUnscaled,
        };

        struct LoadStoreAddressEncoding
        {
            LoadStoreAddressMode mode;
            uint32_t bits;
        };

        constexpr uint32_t register_field(uint32_t encoding, uint8_t shift)
        {
            return encoding << shift;
        }

        template <typename Encoding>
        constexpr uint32_t encoding_bits(Encoding encoding)
        {
            return static_cast<uint32_t>(encoding);
        }

        template <SIMDElementWidth Width>
        consteval uint32_t scalar_fp_type_bits()
        {
            static_assert(Width == SIMDElementWidth::Bits16 ||
                          Width == SIMDElementWidth::Bits32 ||
                          Width == SIMDElementWidth::Bits64);
            if constexpr(Width == SIMDElementWidth::Bits16)
            {
                return 0b11u << 22;
            }
            else if constexpr(Width == SIMDElementWidth::Bits32)
            {
                return 0b00u << 22;
            }
            else
            {
                return 0b01u << 22;
            }
        }

        template <SIMDElementWidth Width>
        consteval uint32_t simd_scalar_size_bits()
        {
            if constexpr(Width == SIMDElementWidth::Bits8)
            {
                return 0u << 30;
            }
            else if constexpr(Width == SIMDElementWidth::Bits16)
            {
                return 1u << 30;
            }
            else if constexpr(Width == SIMDElementWidth::Bits32)
            {
                return 2u << 30;
            }
            else
            {
                static_assert(Width == SIMDElementWidth::Bits64);
                return 3u << 30;
            }
        }

        template <SIMDElementWidth Width>
        consteval uint32_t simd_scalar_byte_width()
        {
            return static_cast<uint32_t>(Width) / 8;
        }

        LoadStoreAddressEncoding
        encode_load_store_address(XRegisterOrSP base, int64_t byte_offset,
                                  uint32_t access_bytes);

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

        inline std::optional<uint16_t>
        try_logical_immediate_64(uint64_t immediate)
        {
            if(immediate == 0 || immediate == UINT64_MAX)
            {
                return std::nullopt;
            }
            for(uint32_t element_size = 2; element_size <= 64;
                element_size *= 2)
            {
                uint64_t element_mask = element_size == 64
                                            ? UINT64_MAX
                                            : (uint64_t{1} << element_size) - 1;
                uint64_t element = immediate & element_mask;
                uint64_t replicated = 0;
                for(uint32_t offset = 0; offset < 64; offset += element_size)
                {
                    replicated |= element << offset;
                }
                if(replicated != immediate)
                {
                    continue;
                }

                for(uint32_t one_bits = 1; one_bits < element_size; ++one_bits)
                {
                    uint64_t ones = (uint64_t{1} << one_bits) - 1;
                    for(uint32_t rotation = 0; rotation < element_size;
                        ++rotation)
                    {
                        uint64_t rotated =
                            rotation == 0
                                ? ones
                                : ((ones >> rotation) |
                                   (ones << (element_size - rotation))) &
                                      element_mask;
                        if(rotated != element)
                        {
                            continue;
                        }

                        uint16_t n = element_size == 64 ? 1 : 0;
                        uint16_t imms = static_cast<uint16_t>(
                            (-(element_size * 2) | (one_bits - 1)) & 0x3f);
                        return static_cast<uint16_t>((n << 12) |
                                                     (rotation << 6) | imms);
                    }
                }
            }
            return std::nullopt;
        }

        inline uint16_t logical_immediate_64(uint64_t immediate)
        {
            std::optional<uint16_t> encoding =
                try_logical_immediate_64(immediate);
            assert(encoding.has_value());
            return *encoding;
        }
    }  // namespace aarch64_detail

    class AArch64Relaxation
    {
    public:
        static constexpr size_t MaximumUnitSize = 128 * 1024 * 1024;

        static AArch64Relaxation unconditional_branch(CodeTarget target,
                                                      XRegister scratch)
        {
            return AArch64Relaxation(target,
                                     AArch64RelaxationKind::UnconditionalBranch,
                                     static_cast<uint8_t>(scratch.encoding()));
        }

        static AArch64Relaxation call(CodeTarget target, XRegister scratch)
        {
            return AArch64Relaxation(target, AArch64RelaxationKind::Call,
                                     static_cast<uint8_t>(scratch.encoding()));
        }

        static AArch64Relaxation conditional_branch(Label target,
                                                    AArch64Condition condition)
        {
            return AArch64Relaxation(target,
                                     AArch64RelaxationKind::ConditionalBranch,
                                     static_cast<uint8_t>(condition));
        }

        const CodeTarget &target() const { return target_; }
        uint32_t min_size() const { return 4; }
        uint32_t max_size() const
        {
            switch(kind_)
            {
                case AArch64RelaxationKind::UnconditionalBranch:
                case AArch64RelaxationKind::Call:
                    return 20;
                case AArch64RelaxationKind::ConditionalBranch:
                    return 8;
            }
            assert(false);
            return 0;
        }

        uint32_t select(MachineAddress source, MachineAddress target);
        void encode(void *write_pointer, MachineAddress source,
                    MachineAddress target) const;

    private:
        static constexpr uint8_t UnselectedMode = 0;
        static constexpr uint8_t NearMode = 1;
        static constexpr uint8_t FarMode = 2;

        AArch64Relaxation(CodeTarget target, AArch64RelaxationKind kind,
                          uint8_t kind_data)
            : target_(target), kind_(kind), kind_data_(kind_data)
        {
        }

        CodeTarget target_;
        AArch64RelaxationKind kind_;
        uint8_t kind_data_;
        uint8_t mode_ = UnselectedMode;
    };

    enum class AArch64RelocationKind : uint8_t
    {
        PcRelative19Scaled4,
        PcRelative21,
        PageRelative21,
        PageOffset12,
        PageOffset12Scaled8,
    };

    class AArch64Relocation
    {
    public:
        AArch64Relocation(ConstantPoolEntry target, AArch64RelocationKind kind)
            : target_(target), kind_(kind)
        {
        }

        RelocationTarget target() const { return target_; }
        void apply(void *write_pointer, MachineAddress instruction_pc,
                   MachineAddress target) const;

    private:
        ConstantPoolEntry target_;
        AArch64RelocationKind kind_;
    };

    using AArch64Emitter =
        MachineCodeEmitter<AArch64Relaxation, AArch64Relocation>;

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

        template <SIMDElementWidth Width>
        void emit_fp_binary(FPBinaryOp operation,
                            SIMDScalarRegister<Width> destination,
                            SIMDScalarRegister<Width> lhs,
                            SIMDScalarRegister<Width> rhs)
        {
            write_instruction(
                0x1e200800 | aarch64_detail::scalar_fp_type_bits<Width>() |
                aarch64_detail::encoding_bits(operation) |
                aarch64_detail::register_field(rhs.encoding(), 16) |
                aarch64_detail::register_field(lhs.encoding(), 5) |
                destination.encoding());
        }

        template <SIMDElementWidth Width>
        void emit_fp_unary(FPUnaryOp operation,
                           SIMDScalarRegister<Width> destination,
                           SIMDScalarRegister<Width> source)
        {
            write_instruction(
                0x1e204000 | aarch64_detail::scalar_fp_type_bits<Width>() |
                aarch64_detail::encoding_bits(operation) |
                aarch64_detail::register_field(source.encoding(), 5) |
                destination.encoding());
        }

        template <SIMDElementWidth Width>
        void emit_fp_compare(SIMDScalarRegister<Width> lhs,
                             SIMDScalarRegister<Width> rhs,
                             FPCompareMode mode = FPCompareMode::Quiet)
        {
            write_instruction(
                0x1e202000 | aarch64_detail::scalar_fp_type_bits<Width>() |
                aarch64_detail::encoding_bits(mode) |
                aarch64_detail::register_field(rhs.encoding(), 16) |
                aarch64_detail::register_field(lhs.encoding(), 5));
        }

        template <SIMDElementWidth Width>
        void emit_fp_compare_zero(SIMDScalarRegister<Width> lhs,
                                  FPCompareMode mode = FPCompareMode::Quiet)
        {
            write_instruction(
                0x1e202008 | aarch64_detail::scalar_fp_type_bits<Width>() |
                aarch64_detail::encoding_bits(mode) |
                aarch64_detail::register_field(lhs.encoding(), 5));
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

        void emit_logical_imm(LogicalOp operation, XRegisterOrZero destination,
                              XRegisterOrZero source, uint64_t immediate)
        {
            uint16_t encoding = aarch64_detail::logical_immediate_64(immediate);
            write_instruction(
                0x92000000 | aarch64_detail::encoding_bits(operation) |
                (static_cast<uint32_t>(encoding) << 10) |
                aarch64_detail::register_field(source.encoding(), 5) |
                destination.encoding());
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

        void emit_multiply_add(XRegisterOrZero destination,
                               XRegisterOrZero multiplicand1,
                               XRegisterOrZero multiplicand2,
                               XRegisterOrZero addend)
        {
            write_instruction(
                0x9b000000 |
                aarch64_detail::register_field(multiplicand2.encoding(), 16) |
                aarch64_detail::register_field(addend.encoding(), 10) |
                aarch64_detail::register_field(multiplicand1.encoding(), 5) |
                destination.encoding());
        }

        void emit_multiply_high(MultiplyHighOp operation,
                                XRegisterOrZero destination,
                                XRegisterOrZero multiplicand1,
                                XRegisterOrZero multiplicand2)
        {
            write_instruction(
                0x9b407c00 | aarch64_detail::encoding_bits(operation) |
                aarch64_detail::register_field(multiplicand2.encoding(), 16) |
                aarch64_detail::register_field(multiplicand1.encoding(), 5) |
                destination.encoding());
        }

        void emit_signed_bitfield_move(XRegisterOrZero destination,
                                       XRegisterOrZero source, uint8_t rotate,
                                       uint8_t source_high_bit)
        {
            assert(rotate < 64);
            assert(source_high_bit < 64);
            write_instruction(
                0x93400000 | (static_cast<uint32_t>(rotate) << 16) |
                (static_cast<uint32_t>(source_high_bit) << 10) |
                aarch64_detail::register_field(source.encoding(), 5) |
                destination.encoding());
        }

        void emit_conditional_select(AArch64Condition condition,
                                     XRegisterOrZero destination,
                                     XRegisterOrZero when_true,
                                     XRegisterOrZero when_false)
        {
            emit_conditional_select(
                GPRWidth::X, condition, destination.encoding(),
                when_true.encoding(), when_false.encoding());
        }

        void emit_conditional_select(AArch64Condition condition,
                                     WRegisterOrZero destination,
                                     WRegisterOrZero when_true,
                                     WRegisterOrZero when_false)
        {
            emit_conditional_select(
                GPRWidth::W, condition, destination.encoding(),
                when_true.encoding(), when_false.encoding());
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

        void emit_load_store_unsigned_offset(LoadStoreOp operation,
                                             XRegisterOrZero value,
                                             XRegisterOrSP base,
                                             uint16_t byte_offset)
        {
            assert(byte_offset % 8 == 0);
            uint32_t scaled_offset = byte_offset / 8;
            assert(scaled_offset < (1 << 12));
            emit_load_store_unsigned_offset(GPRWidth::X, operation,
                                            value.encoding(), base.encoding(),
                                            scaled_offset);
        }

        void emit_load_store_unsigned_offset(LoadStoreOp operation,
                                             WRegisterOrZero value,
                                             XRegisterOrSP base,
                                             uint16_t byte_offset)
        {
            assert(byte_offset % 4 == 0);
            uint32_t scaled_offset = byte_offset / 4;
            assert(scaled_offset < (1 << 12));
            emit_load_store_unsigned_offset(GPRWidth::W, operation,
                                            value.encoding(), base.encoding(),
                                            scaled_offset);
        }

        void emit_load_store_unscaled(LoadStoreOp operation,
                                      XRegisterOrZero value, XRegisterOrSP base,
                                      int16_t byte_offset)
        {
            uint32_t immediate =
                aarch64_detail::signed_immediate(byte_offset, 9, 0);
            emit_load_store_unscaled(GPRWidth::X, operation, value.encoding(),
                                     base.encoding(), immediate);
        }

        void emit_load_store_unscaled(LoadStoreOp operation,
                                      WRegisterOrZero value, XRegisterOrSP base,
                                      int16_t byte_offset)
        {
            uint32_t immediate =
                aarch64_detail::signed_immediate(byte_offset, 9, 0);
            emit_load_store_unscaled(GPRWidth::W, operation, value.encoding(),
                                     base.encoding(), immediate);
        }

        template <SIMDElementWidth Width>
        void emit_load_store_unsigned_offset(LoadStoreOp operation,
                                             SIMDScalarRegister<Width> value,
                                             XRegisterOrSP base,
                                             uint16_t byte_offset)
        {
            constexpr uint32_t AccessBytes =
                aarch64_detail::simd_scalar_byte_width<Width>();
            assert(byte_offset % AccessBytes == 0);
            uint32_t scaled_offset = byte_offset / AccessBytes;
            assert(scaled_offset < (1u << 12));
            write_instruction(
                0x3d000000 | aarch64_detail::simd_scalar_size_bits<Width>() |
                aarch64_detail::encoding_bits(operation) |
                (scaled_offset << 10) |
                aarch64_detail::register_field(base.encoding(), 5) |
                value.encoding());
        }

        template <SIMDElementWidth Width>
        void emit_load_store_unscaled(LoadStoreOp operation,
                                      SIMDScalarRegister<Width> value,
                                      XRegisterOrSP base, int16_t byte_offset)
        {
            uint32_t immediate =
                aarch64_detail::signed_immediate(byte_offset, 9, 0);
            write_instruction(
                0x3c000000 | aarch64_detail::simd_scalar_size_bits<Width>() |
                aarch64_detail::encoding_bits(operation) | (immediate << 12) |
                aarch64_detail::register_field(base.encoding(), 5) |
                value.encoding());
        }

        template <GPRWidth Width>
        void emit_load_store(LoadStoreOp operation, GPRRegister<Width> value,
                             XRegisterOrSP base, int64_t byte_offset)
        {
            constexpr uint32_t AccessBytes =
                Width == GPRWidth::X ? uint32_t{8} : uint32_t{4};
            aarch64_detail::LoadStoreAddressEncoding address =
                aarch64_detail::encode_load_store_address(base, byte_offset,
                                                          AccessBytes);
            uint32_t instruction_base =
                address.mode ==
                        aarch64_detail::LoadStoreAddressMode::UnsignedScaled
                    ? 0xb9000000
                    : 0xb8000000;
            write_instruction(instruction_base |
                              (aarch64_detail::encoding_bits(Width) >> 1) |
                              aarch64_detail::encoding_bits(operation) |
                              address.bits | value.encoding());
        }

        template <SIMDElementWidth Width>
        void emit_load_store(LoadStoreOp operation,
                             SIMDScalarRegister<Width> value,
                             XRegisterOrSP base, int64_t byte_offset)
        {
            constexpr uint32_t AccessBytes =
                aarch64_detail::simd_scalar_byte_width<Width>();
            aarch64_detail::LoadStoreAddressEncoding address =
                aarch64_detail::encode_load_store_address(base, byte_offset,
                                                          AccessBytes);
            uint32_t instruction_base =
                address.mode ==
                        aarch64_detail::LoadStoreAddressMode::UnsignedScaled
                    ? 0x3d000000
                    : 0x3c000000;
            write_instruction(instruction_base |
                              aarch64_detail::simd_scalar_size_bits<Width>() |
                              aarch64_detail::encoding_bits(operation) |
                              address.bits | value.encoding());
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

        void emit_adr_immediate_21(XRegister destination,
                                   int64_t byte_displacement)
        {
            uint32_t immediate =
                aarch64_detail::signed_immediate(byte_displacement, 21, 0);
            uint32_t immediate_low = immediate & 3;
            uint32_t immediate_high = immediate >> 2;
            write_instruction(0x10000000 | (immediate_low << 29) |
                              (immediate_high << 5) | destination.encoding());
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

        void emit_conditional_select(GPRWidth width, AArch64Condition condition,
                                     uint32_t destination, uint32_t when_true,
                                     uint32_t when_false)
        {
            write_instruction(
                0x1a800000 | aarch64_detail::encoding_bits(width) |
                aarch64_detail::register_field(when_false, 16) |
                (static_cast<uint32_t>(condition) << 12) |
                aarch64_detail::register_field(when_true, 5) | destination);
        }

        void emit_load_store_unsigned_offset(GPRWidth width,
                                             LoadStoreOp operation,
                                             uint32_t value, uint32_t base,
                                             uint32_t scaled_offset)
        {
            write_instruction(0xb9000000 |
                              (aarch64_detail::encoding_bits(width) >> 1) |
                              aarch64_detail::encoding_bits(operation) |
                              (scaled_offset << 10) |
                              aarch64_detail::register_field(base, 5) | value);
        }

        void emit_load_store_unscaled(GPRWidth width, LoadStoreOp operation,
                                      uint32_t value, uint32_t base,
                                      uint32_t immediate)
        {
            write_instruction(
                0xb8000000 | (aarch64_detail::encoding_bits(width) >> 1) |
                aarch64_detail::encoding_bits(operation) | (immediate << 12) |
                aarch64_detail::register_field(base, 5) | value);
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
        void madd(XRegisterOrZero destination, XRegisterOrZero multiplicand1,
                  XRegisterOrZero multiplicand2, XRegisterOrZero addend);
        void mul(XRegisterOrZero destination, XRegisterOrZero multiplicand1,
                 XRegisterOrZero multiplicand2);
        void smulh(XRegisterOrZero destination, XRegisterOrZero multiplicand1,
                   XRegisterOrZero multiplicand2);
        void umulh(XRegisterOrZero destination, XRegisterOrZero multiplicand1,
                   XRegisterOrZero multiplicand2);
        void asr(XRegisterOrZero destination, XRegisterOrZero source,
                 uint8_t shift_amount);
        void cmp(XRegisterOrZero left, XRegisterOrZero right);
        void cmp(XRegister left, uint16_t immediate);
        void cmp(WRegisterOrZero left, WRegisterOrZero right);
        void cmn(XRegisterOrZero left, XRegisterOrZero right);
        void cmn(WRegisterOrZero left, WRegisterOrZero right);
        void tst(XRegisterOrZero left, XRegisterOrZero right);
        void tst(XRegisterOrZero source, uint64_t immediate);
        void ldr(XRegister destination, XRegisterOrSP base,
                 int64_t byte_offset);
        void ldr(WRegister destination, XRegisterOrSP base,
                 int64_t byte_offset);
        template <SIMDElementWidth Width>
        void ldr(SIMDScalarRegister<Width> destination, XRegisterOrSP base,
                 int64_t byte_offset)
        {
            emit_load_store(LoadStoreOp::Load, destination, base, byte_offset);
        }
        void ldr(XRegister destination, Value value);
        void ldr(XRegister destination, HeapObject *object);
        void adr(XRegister destination, ConstantPoolEntry target);
        ConstantPoolEntry
        add_transition_program(std::span<const TransitionInstruction> program);
        void str(XRegister source, XRegisterOrSP base, int64_t byte_offset);
        template <SIMDElementWidth Width>
        void str(SIMDScalarRegister<Width> source, XRegisterOrSP base,
                 int64_t byte_offset)
        {
            emit_load_store(LoadStoreOp::Store, source, base, byte_offset);
        }

        void b(CodeTarget target, XRegister scratch = XRegister(16));
        void b(AArch64Condition condition, Label target);
        void bl(CodeTarget target, XRegister scratch = XRegister(16));

    private:
        void ldr(XRegister destination, ConstantPoolEntry entry);

        static constexpr size_t
        maximum_pool_span(AArch64ValuePoolMode pool_mode)
        {
            return pool_mode == AArch64ValuePoolMode::NearLiteral
                       ? 1024 * 1024
                       : AArch64Relaxation::MaximumUnitSize;
        }

        AArch64ValuePoolMode pool_mode_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_ASSEMBLER_H
