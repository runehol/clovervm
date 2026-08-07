#include "jit/aarch64_assembler.h"
#include "jit/machine_address_internal.h"
#include "jit_code_cache_test_support.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

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
        static_assert(DRegister(31).encoding() == 31);
        using V4SRegister = SIMDVectorRegister<SIMDRegisterWidth::Bits128,
                                               SIMDElementWidth::Bits32>;
        static_assert(V4SRegister(31).encoding() == 31);
        static_assert(!std::is_same_v<DRegister, V4SRegister>);

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
        uint32_t instructions[27] = {};
        AArch64BufferAssembler assembler(instructions);

        assembler.emit_arithmetic_imm12(ArithmeticOp::Add, XRegister(5),
                                        XRegister(6), 42);
        assembler.emit_arithmetic_imm12(ArithmeticOp::Add, xsp, xsp, 1,
                                        AddImmediateShift::Twelve);
        assembler.emit_load_store_unsigned_offset(LoadStoreOp::Load,
                                                  XRegister(5), xsp, 24);
        assembler.emit_load_store_unsigned_offset(
            LoadStoreOp::Store, XRegister(5), XRegister(6), 24);
        assembler.emit_load_store_unscaled(LoadStoreOp::Load, XRegister(5),
                                           XRegister(6), -8);
        assembler.emit_move_wide_imm16(MoveWideOp::Movz, XRegister(5), 0x1234,
                                       MoveWideHalfword::Bits16);
        assembler.emit_move_wide_imm16(MoveWideOp::Movk, XRegister(5), 0xabcd,
                                       MoveWideHalfword::Bits48);
        assembler.emit_b_conditional_immediate(AArch64Condition::Equal, 8);
        assembler.emit_arithmetic_imm12(ArithmeticOp::Add, WRegister(5),
                                        WRegister(6), 42);
        assembler.emit_load_store_unsigned_offset(
            LoadStoreOp::Load, WRegister(5), XRegister(6), 12);
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
        assembler.emit_conditional_select(AArch64Condition::Equal, XRegister(5),
                                          XRegister(6), XRegister(7));
        assembler.emit_conditional_select(AArch64Condition::NotEqual,
                                          WRegister(5), WRegister(6),
                                          WRegister(7));
        assembler.emit_adr_immediate_21(XRegister(5), -4);
        assembler.emit_logical_imm(LogicalOp::And, XRegister(16), XRegister(0),
                                   0x1f);
        assembler.emit_logical_imm(LogicalOp::Ands, xzr, XRegister(0), 0x1f);
        assembler.emit_logical_reg(LogicalOp::Ands, xzr, XRegister(0),
                                   XRegister(16));
        assembler.emit_arithmetic_imm12(ArithmeticOp::Subs, xzr, XRegister(16),
                                        4);
        assembler.emit_multiply_add(XRegister(5), XRegister(6), XRegister(7),
                                    XRegister(8));
        assembler.emit_multiply_add(XRegister(5), XRegister(6), XRegister(7),
                                    xzr);
        assembler.emit_multiply_high(MultiplyHighOp::Smulh, XRegister(11),
                                     XRegister(11), XRegister(12));
        assembler.emit_multiply_high(MultiplyHighOp::Umulh, XRegister(11),
                                     XRegister(11), XRegister(12));
        assembler.emit_signed_bitfield_move(XRegister(12), XRegister(20), 5,
                                            63);

        EXPECT_EQ(0x9100a8c5, instructions[0]);
        EXPECT_EQ(0x914007ff, instructions[1]);
        EXPECT_EQ(0xf9400fe5, instructions[2]);
        EXPECT_EQ(0xf9000cc5, instructions[3]);
        EXPECT_EQ(0xf85f80c5, instructions[4]);
        EXPECT_EQ(0xd2a24685, instructions[5]);
        EXPECT_EQ(0xf2f579a5, instructions[6]);
        EXPECT_EQ(0x54000040, instructions[7]);
        EXPECT_EQ(0x1100a8c5, instructions[8]);
        EXPECT_EQ(0xb9400cc5, instructions[9]);
        EXPECT_EQ(0x52a24685, instructions[10]);
        EXPECT_EQ(0x72b579a5, instructions[11]);
        EXPECT_EQ(0xf10004bf, instructions[12]);
        EXPECT_EQ(0xaa0700c5, instructions[13]);
        EXPECT_EQ(0x6a2700c5, instructions[14]);
        EXPECT_EQ(0x9a8700c5, instructions[15]);
        EXPECT_EQ(0x1a8710c5, instructions[16]);
        EXPECT_EQ(0x10ffffe5, instructions[17]);
        EXPECT_EQ(0x92401010, instructions[18]);
        EXPECT_EQ(0xf240101f, instructions[19]);
        EXPECT_EQ(0xea10001f, instructions[20]);
        EXPECT_EQ(0xf100121f, instructions[21]);
        EXPECT_EQ(0x9b0720c5, instructions[22]);
        EXPECT_EQ(0x9b077cc5, instructions[23]);
        EXPECT_EQ(0x9b4c7d6b, instructions[24]);
        EXPECT_EQ(0x9bcc7d6b, instructions[25]);
        EXPECT_EQ(0x9345fe8c, instructions[26]);
    }

    TEST(AArch64Assembler, EmitsOrdinaryInstructionsIntoMachineCodeEmitter)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();

        assembler.mov(XRegister(5), 0x123400005678ULL);

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        const void *code = allocation.writable_code().data();
        EXPECT_EQ(0xd28acf05, instruction_at(code, 0));
        EXPECT_EQ(0xf2c24685, instruction_at(code, 1));
    }

    TEST(AArch64Assembler, EmitsMacroInstructionsThroughEncodingFamilies)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();

        assembler.mov(XRegister(5), XRegister(6));
        assembler.mvn(XRegister(5), XRegister(6));
        assembler.neg(XRegister(5), XRegister(6));
        assembler.cmp(XRegister(5), XRegister(6));
        assembler.cmn(XRegister(5), XRegister(6));
        assembler.ldr(XRegister(5), XRegister(6), 24);
        assembler.str(XRegister(5), XRegister(6), -8);

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));
        const void *code = allocation.writable_code().data();
        EXPECT_EQ(0xaa0603e5, instruction_at(code, 0));
        EXPECT_EQ(0xaa2603e5, instruction_at(code, 1));
        EXPECT_EQ(0xcb0603e5, instruction_at(code, 2));
        EXPECT_EQ(0xeb0600bf, instruction_at(code, 3));
        EXPECT_EQ(0xab0600bf, instruction_at(code, 4));
        EXPECT_EQ(0xf9400cc5, instruction_at(code, 5));
        EXPECT_EQ(0xf81f80c5, instruction_at(code, 6));
    }

    TEST(AArch64Assembler, RelaxesNearAndFarConditionalBranches)
    {
        {
            CacheAndPlatform fixture(16);
            AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
            Label target = assembler.emitter().make_label();
            assembler.b(AArch64Condition::Equal, target);
            assembler.mov(XRegister(0), XRegister(1));
            assembler.emitter().resolve(target);
            assembler.emit_ret();

            CodeAllocation allocation =
                take_allocation(assembler.emitter().finalize(*fixture.cache));
            EXPECT_EQ(0x54000040u,
                      instruction_at(allocation.writable_code().data(), 0));
        }

        {
            constexpr size_t PaddingSize = 1024 * 1024;
            CacheAndPlatform fixture(16, 2 * PaddingSize);
            AArch64MacroAssembler assembler(
                AArch64ValuePoolMode::FarPageRelative);
            Label target = assembler.emitter().make_label();
            assembler.b(AArch64Condition::Equal, target);
            std::vector<std::byte> padding(PaddingSize);
            assembler.emitter().emit_bytes(padding.data(), padding.size());
            assembler.emitter().resolve(target);
            assembler.emit_ret();

            CodeAllocation allocation =
                take_allocation(assembler.emitter().finalize(*fixture.cache));
            const void *code = allocation.writable_code().data();
            EXPECT_EQ(0x54000041u, instruction_at(code, 0));
            uint32_t expected_branch =
                0x14000000u |
                (static_cast<uint32_t>((PaddingSize + 4) >> 2) & 0x3ffffffu);
            EXPECT_EQ(expected_branch, instruction_at(code, 1));
        }
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
                allocation.constant_pool_address());
        uint32_t expected =
            0x58000005 |
            ((static_cast<uint32_t>(displacement >> 2) & 0x7ffff) << 5);
        EXPECT_EQ(expected,
                  instruction_at(allocation.writable_code().data(), 0));
        EXPECT_EQ(Value::True(), reinterpret_cast<Value *>(
                                     allocation.constant_pool().data())[0]);
    }

    TEST(AArch64Assembler, RelocatesFarValuePoolLoad)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::FarPageRelative);
        AArch64Emitter &emitter = assembler.emitter();
        assembler.ldr(XRegister(5), Value::None());

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));

        EXPECT_EQ(0xf0000065,
                  instruction_at(allocation.writable_code().data(), 0));
        EXPECT_EQ(0xf947fca5,
                  instruction_at(allocation.writable_code().data(), 1));
    }

    TEST(AArch64Assembler, RelocatesNearConstantPoolAddress)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();
        std::byte data[] = {std::byte{0x11}};
        ConstantPoolEntry entry = emitter.add_data_to_constant_pool(data);
        assembler.adr(XRegister(5), entry);

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));

        int64_t displacement =
            allocation.code.execute_address().displacement_to(
                allocation.constant_pool_address());
        uint32_t immediate = static_cast<uint32_t>(displacement) & 0x1fffff;
        uint32_t expected =
            0x10000005 | ((immediate & 3) << 29) | ((immediate >> 2) << 5);
        EXPECT_EQ(expected,
                  instruction_at(allocation.writable_code().data(), 0));
    }

    TEST(AArch64Assembler, RelocatesFarConstantPoolAddress)
    {
        CacheAndPlatform fixture(16);
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::FarPageRelative);
        AArch64Emitter &emitter = assembler.emitter();
        std::byte data[] = {std::byte{0x11}};
        ConstantPoolEntry entry = emitter.add_data_to_constant_pool(data);
        assembler.adr(XRegister(5), entry);

        CodeAllocation allocation =
            take_allocation(emitter.finalize(*fixture.cache));

        uint32_t expected[2] = {};
        AArch64BufferAssembler expected_assembler(expected);
        MachineAddress instruction_pc = allocation.code.execute_address();
        MachineAddress target = allocation.constant_pool_address();
        expected_assembler.emit_adrp_page_immediate_21(
            XRegister(5), instruction_pc.aligned_displacement_to(target, 12));
        expected_assembler.emit_arithmetic_imm12(
            ArithmeticOp::Add, XRegister(5), XRegister(5),
            static_cast<uint16_t>(target.offset_within(12)));
        EXPECT_EQ(expected[0],
                  instruction_at(allocation.writable_code().data(), 0));
        EXPECT_EQ(expected[1],
                  instruction_at(allocation.writable_code().data(), 1));
    }

    TEST(AArch64Assembler, RelocationsAddToEncodedImmediates)
    {
        AArch64MacroAssembler owner(AArch64ValuePoolMode::NearLiteral);
        ConstantPoolEntry target_entry =
            owner.emitter().add_value_to_constant_pool(Value::None());

        uint32_t instruction = 0;
        MachineAddress pc = detail::MachineAddressAccess::from_bits(0x00001000);
        MachineAddress target =
            detail::MachineAddressAccess::from_bits(0x00001100);

        AArch64BufferAssembler(&instruction)
            .emit_ldr_literal_immediate_19(XRegister(5), 8);
        AArch64Relocation(target_entry,
                          AArch64RelocationKind::PcRelative19Scaled4)
            .apply(&instruction, pc, target);
        EXPECT_EQ(0x58000845, instruction);

        AArch64BufferAssembler(&instruction)
            .emit_adr_immediate_21(XRegister(5), 8);
        AArch64Relocation(target_entry, AArch64RelocationKind::PcRelative21)
            .apply(&instruction, pc, target);
        EXPECT_EQ(0x10000845, instruction);

        AArch64BufferAssembler(&instruction)
            .emit_adrp_page_immediate_21(XRegister(5), -4096);
        target = detail::MachineAddressAccess::from_bits(0x00003000);
        AArch64Relocation(target_entry, AArch64RelocationKind::PageRelative21)
            .apply(&instruction, pc, target);
        EXPECT_EQ(0xb0000005, instruction);

        AArch64BufferAssembler(&instruction)
            .emit_arithmetic_imm12(ArithmeticOp::Add, XRegister(5),
                                   XRegister(5), 24);
        target = detail::MachineAddressAccess::from_bits(0x00002100);
        AArch64Relocation(target_entry, AArch64RelocationKind::PageOffset12)
            .apply(&instruction, pc, target);
        EXPECT_EQ(0x910460a5, instruction);

        AArch64BufferAssembler(&instruction)
            .emit_load_store_unsigned_offset(LoadStoreOp::Load, XRegister(5),
                                             XRegister(5), 24);
        target = detail::MachineAddressAccess::from_bits(0x00002100);
        AArch64Relocation(target_entry,
                          AArch64RelocationKind::PageOffset12Scaled8)
            .apply(&instruction, pc, target);
        EXPECT_EQ(0xf9408ca5, instruction);
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
        const void *code = allocation.writable_code().data();
        EXPECT_EQ(0x14000004, instruction_at(code, 0));
        EXPECT_EQ(0xd28acf10, instruction_at(code, 1));
        EXPECT_EQ(0xf2a00010, instruction_at(code, 2));
        EXPECT_EQ(0xf2c00010, instruction_at(code, 3));
        EXPECT_EQ(0xf2e24690, instruction_at(code, 4));
        EXPECT_EQ(0xd63f0200, instruction_at(code, 5));
    }

}  // namespace cl::jit
