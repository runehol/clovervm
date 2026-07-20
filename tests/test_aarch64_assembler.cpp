#include "jit/aarch64_assembler.h"
#include "jit/machine_address_internal.h"
#include "jit_code_cache_test_support.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace cl::jit
{
    namespace
    {
        using test_support::CacheAndPlatform;

        static_assert(std::is_constructible_v<XRegisterOrSP, XRegister>);
        static_assert(std::is_constructible_v<XRegisterOrZero, XRegister>);
        static_assert(std::is_constructible_v<XRegisterOrSP, XSP>);
        static_assert(!std::is_constructible_v<XRegisterOrSP, XZero>);
        static_assert(std::is_constructible_v<XRegisterOrZero, XZero>);
        static_assert(!std::is_constructible_v<XRegisterOrZero, XSP>);
        static_assert(!std::is_constructible_v<XRegisterOrSP, WRegister>);

        uint32_t instruction_at(const void *code, size_t index)
        {
            uint32_t result;
            std::memcpy(&result,
                        static_cast<const uint8_t *>(code) +
                            index * sizeof(uint32_t),
                        sizeof(result));
            return result;
        }

        CodeAllocation
        take_allocation(Result<CodeAllocation, JitCodeError> result)
        {
            EXPECT_TRUE(result);
            return std::move(result).value();
        }
    }  // namespace

    TEST(AArch64Assembler, EncodesRepresentativeExactInstructions)
    {
        uint32_t instructions[13] = {};
        AArch64BufferAssembler assembler(instructions);

        assembler.emit_arithmetic_imm12(ArithmeticOp::Add, XRegister(5),
                                        XRegister(6), 42);
        assembler.emit_arithmetic_imm12(ArithmeticOp::Add, xsp, xsp, 1,
                                        AddImmediateShift::Twelve);
        assembler.emit_ldr_unsigned_offset(XRegister(5), xsp, 24);
        assembler.emit_move_wide_imm16(MoveWideOp::Movz, XRegister(5), 0x1234,
                                       MoveWideHalfword::Bits16);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, XRegister(5), 0xabcd,
                                       MoveWideHalfword::Bits48);
        assembler.emit_b_conditional_immediate(AArch64Condition::Equal, 8);
        assembler.emit_arithmetic_imm12(ArithmeticOp::Add, WRegister(5),
                                        WRegister(6), 42);
        assembler.emit_ldr_unsigned_offset(WRegister(5), XRegister(6), 12);
        assembler.emit_move_wide_imm16(MoveWideOp::Movz, WRegister(5), 0x1234,
                                       MoveWideHalfword::Bits16);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, WRegister(5), 0xabcd,
                                       MoveWideHalfword::Bits16);
        assembler.emit_arithmetic_imm12(ArithmeticOp::Subs, xzr, XRegister(5),
                                        1);
        assembler.emit_logical_reg(LogicalOp::Orr, XRegister(5), XRegister(6),
                                   XRegister(7));
        assembler.emit_logical_reg(LogicalOp::Ands, WRegister(5), WRegister(6),
                                   WRegister(7), InvertMode::Invert);

        EXPECT_EQ(0x9100a8c5, instructions[0]);
        EXPECT_EQ(0x914007ff, instructions[1]);
        EXPECT_EQ(0xf9400fe5, instructions[2]);
        EXPECT_EQ(0xd2a24685, instructions[3]);
        EXPECT_EQ(0xf2f579a5, instructions[4]);
        EXPECT_EQ(0x54000040, instructions[5]);
        EXPECT_EQ(0x1100a8c5, instructions[6]);
        EXPECT_EQ(0xb9400cc5, instructions[7]);
        EXPECT_EQ(0x52a24685, instructions[8]);
        EXPECT_EQ(0x72b579a5, instructions[9]);
        EXPECT_EQ(0xf10004bf, instructions[10]);
        EXPECT_EQ(0xaa0700c5, instructions[11]);
        EXPECT_EQ(0x6a2700c5, instructions[12]);
    }

    TEST(AArch64Assembler, EmitsOrdinaryInstructionsIntoMachineCodeEmitter)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();

        assembler.mov(XRegister(5), 0x123400005678ULL);

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        const void *code = allocation.write_pointer();
        EXPECT_EQ(0xd28acf05, instruction_at(code, 0));
        EXPECT_EQ(0xf2c24685, instruction_at(code, 1));
    }

    TEST(AArch64Assembler, EmitsAliasesThroughEncodingFamilies)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();

        assembler.mov(XRegister(5), XRegister(6));
        assembler.mvn(XRegister(5), XRegister(6));
        assembler.neg(XRegister(5), XRegister(6));
        assembler.cmp(XRegister(5), XRegister(6));
        assembler.cmn(XRegister(5), XRegister(6));

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        const void *code = allocation.write_pointer();
        EXPECT_EQ(0xaa0603e5, instruction_at(code, 0));
        EXPECT_EQ(0xaa2603e5, instruction_at(code, 1));
        EXPECT_EQ(0xcb0603e5, instruction_at(code, 2));
        EXPECT_EQ(0xeb0600bf, instruction_at(code, 3));
        EXPECT_EQ(0xab0600bf, instruction_at(code, 4));
    }

    TEST(AArch64Assembler, RelocatesNearValuePoolLoad)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();
        assembler.ldr(XRegister(5), Value::True());

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));

        int64_t displacement =
            allocation.code.execute_address().displacement_to(
                allocation.value_pool.address());
        uint32_t expected =
            0x58000005 |
            ((static_cast<uint32_t>(displacement >> 2) & 0x7ffff) << 5);
        EXPECT_EQ(expected, instruction_at(allocation.write_pointer(), 0));
        EXPECT_EQ(Value::True(), allocation.value_pool.write_pointer()[0]);
    }

    TEST(AArch64Assembler, RelocatesFarValuePoolLoad)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::FarPageRelative);
        AArch64Emitter &emitter = assembler.emitter();
        assembler.ldr(XRegister(5), Value::None());

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));

        EXPECT_EQ(0xf0000065, instruction_at(allocation.write_pointer(), 0));
        EXPECT_EQ(0xf947fca5, instruction_at(allocation.write_pointer(), 1));
    }

    TEST(AArch64Assembler, SelectsDirectAndSynthesizedBranches)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();
        assembler.b(detail::MachineAddressAccess::from_bits(0x10000010));
        assembler.bl(
            detail::MachineAddressAccess::from_bits(0x1234000000005678));

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        const void *code = allocation.write_pointer();
        EXPECT_EQ(0x14000004, instruction_at(code, 0));
        EXPECT_EQ(0xd28acf10, instruction_at(code, 1));
        EXPECT_EQ(0xf2a00010, instruction_at(code, 2));
        EXPECT_EQ(0xf2c00010, instruction_at(code, 3));
        EXPECT_EQ(0xf2e24690, instruction_at(code, 4));
        EXPECT_EQ(0xd63f0200, instruction_at(code, 5));
    }

}  // namespace cl::jit
