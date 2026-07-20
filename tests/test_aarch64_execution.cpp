#include "jit/aarch64_assembler.h"
#include "jit/standard_code_memory.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace cl::jit
{
    TEST(AArch64Execution, CallsGeneratedLeafFunction)
    {
        CodeCache cache(std::make_unique<StandardCodeMemory>());
        AArch64Emitter emitter;
        AArch64MacroAssembler assembler(emitter,
                                        AArch64ValuePoolMode::NearLiteral);

        assembler.emit_arithmetic_reg(ArithmeticOp::Add, XRegister(0),
                                      XRegister(0), XRegister(1));
        assembler.emit_ret();

        Result<CodeAllocation, JitCodeError> finalization =
            emitter.finalize(cache, 1);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        Result<JitCodeObject *, JitCodeError> publication =
            cache.publish(allocation);
        ASSERT_TRUE(publication);
        JitCodeObject *code = std::move(publication).value();

        using Function = int64_t (*)(int64_t, int64_t);
        Function function = reinterpret_cast<Function>(
            code->entry().bits_for_indirect_target());
        EXPECT_EQ(42, function(19, 23));
    }

    TEST(AArch64Execution, CallsGeneratedFunctionWithBranches)
    {
        CodeCache cache(std::make_unique<StandardCodeMemory>());
        AArch64Emitter emitter;
        AArch64MacroAssembler assembler(emitter,
                                        AArch64ValuePoolMode::NearLiteral);
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
            emitter.finalize(cache, 1);
        ASSERT_TRUE(finalization);
        CodeAllocation allocation = std::move(finalization).value();

        Result<JitCodeObject *, JitCodeError> publication =
            cache.publish(allocation);
        ASSERT_TRUE(publication);
        JitCodeObject *code = std::move(publication).value();

        using Function = int64_t (*)(int64_t, int64_t);
        Function function = reinterpret_cast<Function>(
            code->entry().bits_for_indirect_target());
        EXPECT_EQ(5, function(9, 4));
        EXPECT_EQ(5, function(4, 9));
        EXPECT_EQ(0, function(7, 7));
    }
}  // namespace cl::jit
