#include "jit/transition_executor.h"

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

        uint64_t word_for(ThreadState *thread_state)
        {
            static_assert(sizeof(thread_state) == sizeof(uint64_t));
            uint64_t result;
            std::memcpy(&result, &thread_state, sizeof(result));
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
        Value expected = Value::from_smi(123);
        std::array<Value, 8> stack = {};
        Value *frame_pointer = stack.data() + 4;
        ThreadState *expected_thread_state =
            reinterpret_cast<ThreadState *>(stack.data());
        std::array<uint64_t, 2> register_file = {
            word_for(expected_thread_state), word_for(expected)};

        TransitionProgramBuilder builder;
        builder.emplace_transfer(TransitionLocation::scratch(0),
                                 TransitionLocation::register_file(1));
        builder.emplace_transfer(TransitionLocation::stack(-2),
                                 TransitionLocation::scratch(0));
        builder.emplace_resume_interpreter(TransitionLocation::stack(-2),
                                           TransitionLocation::register_file(0),
                                           41);
        std::vector<TransitionInstruction> instructions =
            std::move(builder).finalize();

        TransitionExecutionContext context;
        InterpreterResumeState result = execute_transition_program(
            context, instructions, {register_file, frame_pointer});

        EXPECT_EQ(expected_thread_state, result.thread_state);
        EXPECT_EQ(expected, result.accumulator);
        EXPECT_EQ(41u, result.resume_pc);
        EXPECT_EQ(word_for(expected), stack_word(frame_pointer - 2));
    }

    TEST(TransitionExecutor, PreservesArbitraryTransferBits)
    {
        constexpr uint64_t Bits = 0xfedcba9876543210;
        Value accumulator = Value::from_smi(1);
        std::array<Value, 8> stack = {};
        Value *frame_pointer = stack.data() + 4;
        ThreadState *expected_thread_state =
            reinterpret_cast<ThreadState *>(stack.data());
        std::array<uint64_t, 3> register_file = {
            Bits, word_for(accumulator), word_for(expected_thread_state)};

        TransitionProgramBuilder builder;
        builder.emplace_transfer(TransitionLocation::stack(1),
                                 TransitionLocation::register_file(0));
        builder.emplace_resume_interpreter(TransitionLocation::register_file(1),
                                           TransitionLocation::register_file(2),
                                           9);
        std::vector<TransitionInstruction> instructions =
            std::move(builder).finalize();

        TransitionExecutionContext context;
        InterpreterResumeState result = execute_transition_program(
            context, instructions, {register_file, frame_pointer});

        EXPECT_EQ(expected_thread_state, result.thread_state);
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
        EXPECT_DEATH(
            {
                TransitionProgramBuilder builder;
                builder.emplace_transfer(TransitionLocation::register_file(0),
                                         TransitionLocation::stack(-1));
                builder.emplace_resume_interpreter(
                    TransitionLocation::stack(-1),
                    TransitionLocation::register_file(0), 3);
                (void)std::move(builder).finalize();
            },
            "writes its register file");
    }

}  // namespace cl::jit
