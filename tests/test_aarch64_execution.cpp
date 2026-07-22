#include "jit/aarch64_assembler.h"
#include "jit/aarch64_cfg_emitter.h"
#include "jit/compilation_arena.h"
#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace cl::jit
{
    namespace
    {
        template <typename LogicalInstruction>
        uint64_t execute_smi_logical_with_identical_operands(Value input)
        {
            CompilationArena arena;
            GraphBuilder builder(arena);
            Block *entry = builder.emplace_block();
            ParameterInstruction *parameter =
                builder.emplace_parameter<ParameterInstruction>(entry);
            TaggedValueRef operand(parameter);
            LogicalInstruction *result =
                builder.emplace_instruction<LogicalInstruction>(entry, operand,
                                                                operand);
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(result));
            ControlFlowGraph *graph = builder.finalize();

            CodeCache cache;
            Result<JitCodeObject *, JitCodeError> emission =
                emit_aarch64_from_cfg(*graph, cache);
            EXPECT_TRUE(emission);
            if(!emission)
            {
                return 0;
            }
            JitCodeObject *code = std::move(emission).value();

            using Function = uint64_t (*)(uint64_t);
            Function function = reinterpret_cast<Function>(
                code->entry().bits_for_indirect_target());
            return function(static_cast<uint64_t>(input.as.integer));
        }
    }  // namespace

    TEST(AArch64Execution, EmitsIdentityFunctionFromCfg)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        CodeCache cache;
        Result<JitCodeObject *, JitCodeError> emission =
            emit_aarch64_from_cfg(*graph, cache);
        ASSERT_TRUE(emission);
        JitCodeObject *code = std::move(emission).value();

        using Function = uint64_t (*)(uint64_t);
        Function function = reinterpret_cast<Function>(
            code->entry().bits_for_indirect_target());
        const uint64_t inputs[] = {
            static_cast<uint64_t>(Value::None().as.integer),
            static_cast<uint64_t>(Value::True().as.integer),
            static_cast<uint64_t>(Value::from_smi(42).as.integer),
        };
        for(uint64_t input: inputs)
        {
            EXPECT_EQ(input, function(input));
        }
    }

    TEST(AArch64Execution, EmitsInlineConstantFunctionFromCfg)
    {
        CompilationArena arena;
        GraphBuilder builder(arena);
        Block *entry = builder.emplace_block();
        Value expected = Value::from_smi(0x123456789abcd);
        ConstInstruction *constant =
            builder.emplace_instruction<ConstInstruction>(entry, expected);
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(constant));
        ControlFlowGraph *graph = builder.finalize();

        CodeCache cache;
        Result<JitCodeObject *, JitCodeError> emission =
            emit_aarch64_from_cfg(*graph, cache);
        ASSERT_TRUE(emission);
        JitCodeObject *code = std::move(emission).value();

        using Function = uint64_t (*)();
        Function function = reinterpret_cast<Function>(
            code->entry().bits_for_indirect_target());
        EXPECT_EQ(static_cast<uint64_t>(expected.as.integer), function());
    }

    TEST(AArch64Execution, EmitsAndSmiFromCfg)
    {
        Value input = Value::from_smi(-0x123456789abcd);
        EXPECT_EQ(
            static_cast<uint64_t>(input.as.integer),
            execute_smi_logical_with_identical_operands<AndSMIInstruction>(
                input));
    }

    TEST(AArch64Execution, EmitsOrrSmiFromCfg)
    {
        Value input = Value::from_smi(-0x123456789abcd);
        EXPECT_EQ(
            static_cast<uint64_t>(input.as.integer),
            execute_smi_logical_with_identical_operands<OrrSMIInstruction>(
                input));
    }

    TEST(AArch64Execution, EmitsEorSmiFromCfg)
    {
        Value input = Value::from_smi(-0x123456789abcd);
        EXPECT_EQ(
            static_cast<uint64_t>(Value::from_smi(0).as.integer),
            execute_smi_logical_with_identical_operands<EorSMIInstruction>(
                input));
    }

    TEST(AArch64Execution, CallsGeneratedLeafFunction)
    {
        CodeCache cache;
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();

        assembler.emit_arithmetic_reg(ArithmeticOp::Add, XRegister(0),
                                      XRegister(0), XRegister(1));
        assembler.emit_ret();

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        Result<JitCodeObject *, JitCodeError> publication =
            cache.publish(std::move(allocation));
        ASSERT_TRUE(publication);
        JitCodeObject *code = std::move(publication).value();

        using Function = int64_t (*)(int64_t, int64_t);
        Function function = reinterpret_cast<Function>(
            code->entry().bits_for_indirect_target());
        EXPECT_EQ(42, function(19, 23));
    }

    TEST(AArch64Execution, CallsGeneratedFunctionWithBranches)
    {
        CodeCache cache;
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();
        Label done = emitter.make_label();

        assembler.cmp(XRegister(0), XRegister(1));
        assembler.emit_b_conditional_immediate(
            AArch64Condition::SignedGreaterOrEqual, 12);
        assembler.emit_arithmetic_reg(ArithmeticOp::Sub, XRegister(0),
                                      XRegister(1), XRegister(0));
        assembler.b(done);
        assembler.emit_arithmetic_reg(ArithmeticOp::Sub, XRegister(0),
                                      XRegister(0), XRegister(1));
        emitter.resolve(done);
        assembler.emit_ret();

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        Result<JitCodeObject *, JitCodeError> publication =
            cache.publish(std::move(allocation));
        ASSERT_TRUE(publication);
        JitCodeObject *code = std::move(publication).value();

        using Function = int64_t (*)(int64_t, int64_t);
        Function function = reinterpret_cast<Function>(
            code->entry().bits_for_indirect_target());
        EXPECT_EQ(5, function(9, 4));
        EXPECT_EQ(5, function(4, 9));
        EXPECT_EQ(0, function(7, 7));
    }

    TEST(AArch64Execution, LoadsAndRewritesValueFromPreferredConstantPool)
    {
        CodeCache cache;
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::NearLiteral);
        AArch64Emitter &emitter = assembler.emitter();
        assembler.ldr(XRegister(0), Value::True());
        assembler.emit_ret();

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        Result<JitCodeObject *, JitCodeError> publication =
            cache.publish(std::move(allocation));
        ASSERT_TRUE(publication);
        JitCodeObject *code = std::move(publication).value();

        using Function = uint64_t (*)();
        Function function = reinterpret_cast<Function>(
            code->entry().bits_for_indirect_target());
        EXPECT_EQ(static_cast<uint64_t>(Value::True().as.integer), function());

        code->value_pool().write_pointer()[0] = Value::False();
        EXPECT_EQ(static_cast<uint64_t>(Value::False().as.integer), function());
    }

    TEST(AArch64Execution, LoadsAndRewritesValueFromFarConstantPool)
    {
        CodeCache cache;
        AArch64MacroAssembler assembler(AArch64ValuePoolMode::FarPageRelative);
        AArch64Emitter &emitter = assembler.emitter();
        assembler.ldr(XRegister(0), Value::True());
        assembler.emit_ret();

        constexpr size_t PaddingSize = 2 * 1024 * 1024;
        for(size_t emitted = 0; emitted < PaddingSize;
            emitted += sizeof(uint32_t))
        {
            assembler.emit_ret();
        }

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        Result<JitCodeObject *, JitCodeError> publication =
            cache.publish(std::move(allocation));
        ASSERT_TRUE(publication);
        JitCodeObject *code = std::move(publication).value();

        using Function = uint64_t (*)();
        Function function = reinterpret_cast<Function>(
            code->entry().bits_for_indirect_target());
        EXPECT_EQ(static_cast<uint64_t>(Value::True().as.integer), function());

        code->value_pool().write_pointer()[0] = Value::False();
        EXPECT_EQ(static_cast<uint64_t>(Value::False().as.integer), function());
    }
}  // namespace cl::jit
