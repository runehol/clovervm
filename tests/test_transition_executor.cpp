#include "jit/transition_executor.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        uint64_t word_for(Value value)
        {
            static_assert(sizeof(value) == sizeof(uint64_t));
            uint64_t result;
            std::memcpy(&result, &value, sizeof(result));
            return result;
        }

        uint64_t stack_word(Value *location)
        {
            uint64_t result;
            std::memcpy(&result, location, sizeof(result));
            return result;
        }
    }  // namespace

    TEST(TransitionExecutor, TransfersThroughScratchAndReturnsResumeState)
    {
        test::VmTestContext vm;
        CodeObject *code_object = vm.compile_file(L"");
        Value expected = Value::from_smi(123);
        std::array<Value, 8> stack = {};
        Value *frame_pointer = stack.data() + 4;
        TransitionProgramBuilder builder;
        builder.emplace_transfer(TransitionLocation::scratch(0),
                                 TransitionLocation::register_file(1));
        builder.emplace_transfer(TransitionLocation::stack(-2),
                                 TransitionLocation::scratch(0));
        builder.emplace_resume_interpreter(code_object, 0);
        std::vector<TransitionInstruction> instructions =
            std::move(builder).finalize();

        TransitionExecutionContext context;
        context.register_file()[1] = word_for(expected);
        const InterpreterResumeState *result = cl_execute_transition_program(
            &context, instructions.data(), frame_pointer);

        EXPECT_EQ(expected, result->accumulator);
        EXPECT_EQ(code_object->code.data(), result->pc);
        EXPECT_EQ(code_object, result->code_object);
        EXPECT_EQ(word_for(expected), stack_word(frame_pointer - 2));
    }

    TEST(TransitionExecutor, PreservesArbitraryTransferBits)
    {
        test::VmTestContext vm;
        CodeObject *code_object = vm.compile_file(L"");
        constexpr uint64_t Bits = 0xfedcba9876543210;
        Value accumulator = Value::from_smi(1);
        std::array<Value, 8> stack = {};
        Value *frame_pointer = stack.data() + 4;
        TransitionProgramBuilder builder;
        builder.emplace_transfer(TransitionLocation::stack(1),
                                 TransitionLocation::register_file(0));
        builder.emplace_transfer(TransitionLocation::scratch(0),
                                 TransitionLocation::register_file(1));
        builder.emplace_resume_interpreter(code_object, 0);
        std::vector<TransitionInstruction> instructions =
            std::move(builder).finalize();

        TransitionExecutionContext context;
        context.register_file()[0] = Bits;
        context.register_file()[1] = word_for(accumulator);
        const InterpreterResumeState *result = cl_execute_transition_program(
            &context, instructions.data(), frame_pointer);

        EXPECT_EQ(accumulator, result->accumulator);
        EXPECT_EQ(code_object, result->code_object);
        EXPECT_EQ(Bits, stack_word(frame_pointer + 1));
    }

    TEST(TransitionExecutionContext, ReusesAndGrowsScratchStorage)
    {
        TransitionExecutionContext context;
        std::span<uint64_t> initial = context.ensure_scratch(2);
        initial[0] = 17;

        std::span<uint64_t> same_size = context.ensure_scratch(2);
        EXPECT_EQ(17u, same_size[0]);

        std::span<uint64_t> grown = context.ensure_scratch(8);
        EXPECT_EQ(8u, grown.size());
        EXPECT_EQ(17u, grown[0]);
    }

    TEST(TransitionProgramVerifier, RejectsRegisterFileDestination)
    {
        test::VmTestContext vm;
        CodeObject *code_object = vm.compile_file(L"");
        EXPECT_DEATH(
            {
                TransitionProgramBuilder builder;
                builder.emplace_transfer(TransitionLocation::register_file(0),
                                         TransitionLocation::stack(-1));
                builder.emplace_resume_interpreter(code_object, 0);
                (void)std::move(builder).finalize();
            },
            "writes its register file");
    }

}  // namespace cl::jit
