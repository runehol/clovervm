#include "jit/transition_program.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cl::jit
{
    TEST(TransitionLocation, PreservesAreaAndOffset)
    {
        TransitionLocation register_file =
            TransitionLocation::register_file(17);
        TransitionLocation stack = TransitionLocation::stack(-12);
        TransitionLocation scratch = TransitionLocation::scratch(23);

        EXPECT_EQ(TransitionLocationArea::RegisterFile, register_file.area());
        EXPECT_EQ(17, register_file.offset());
        EXPECT_EQ(TransitionLocationArea::Stack, stack.area());
        EXPECT_EQ(-12, stack.offset());
        EXPECT_EQ(TransitionLocationArea::Scratch, scratch.area());
        EXPECT_EQ(23, scratch.offset());
        EXPECT_EQ(stack, TransitionLocation::stack(-12));
    }

    TEST(TransitionInstruction, ConstructsTransitionSpecificInstructions)
    {
        TransitionInstruction begin =
            TransitionInstruction::begin_transition(0);
        TransitionInstruction transfer = TransitionInstruction::transfer(
            TransitionLocation::stack(-4),
            TransitionLocation::register_file(1));
        TransitionInstruction resume =
            TransitionInstruction::resume_interpreter(
                TransitionLocation::scratch(2), 37);

        EXPECT_EQ(TransitionInstructionKind::BeginTransition, begin.kind());
        EXPECT_EQ(0u, begin.scratch_slot_count());
        EXPECT_EQ(TransitionInstructionKind::Transfer, transfer.kind());
        EXPECT_EQ(TransitionLocation::register_file(1),
                  transfer.transfer_source());
        EXPECT_EQ(TransitionLocation::stack(-4),
                  transfer.transfer_destination());
        EXPECT_EQ(TransitionInstructionKind::ResumeInterpreter, resume.kind());
        EXPECT_EQ(TransitionLocation::scratch(2),
                  resume.interpreter_accumulator());
        EXPECT_EQ(37u, resume.resume_pc());
    }

    TEST(TransitionInstruction, PatchesInitialScratchRequirement)
    {
        std::vector<TransitionInstruction> instructions;
        instructions.push_back(TransitionInstruction::begin_transition(0));
        instructions.push_back(TransitionInstruction::transfer(
            TransitionLocation::scratch(3),
            TransitionLocation::register_file(0)));
        instructions.push_back(TransitionInstruction::resume_interpreter(
            TransitionLocation::scratch(3), 12));

        instructions.front().set_scratch_slot_count(4);

        EXPECT_EQ(4u, instructions.front().scratch_slot_count());
        EXPECT_EQ(TransitionInstructionKind::Transfer, instructions[1].kind());
    }

    TEST(TransitionInstruction, UsesInstructionCompatibleKinds)
    {
        static_assert(
            std::is_same_v<std::underlying_type_t<TransitionInstructionKind>,
                           std::underlying_type_t<InstructionKind>>);
        EXPECT_LE(
            static_cast<uint16_t>(InstructionOrdinal::Count),
            static_cast<uint16_t>(TransitionInstructionKind::BeginTransition));
    }

    TEST(TransitionProgramBuilder, KeepsScratchCountCurrentWhileAppending)
    {
        TransitionProgramBuilder builder;
        builder.emplace_transfer(TransitionLocation::scratch(3),
                                 TransitionLocation::stack(-2));
        builder.emplace_transfer(TransitionLocation::stack(-1),
                                 TransitionLocation::scratch(3));
        builder.emplace_resume_interpreter(TransitionLocation::stack(-1), 42);

        std::vector<TransitionInstruction> instructions =
            std::move(builder).finalize();

        ASSERT_EQ(4u, instructions.size());
        EXPECT_EQ(4u, instructions.front().scratch_slot_count());
    }

    TEST(TransitionProgramBuilder, DirectTransfersNeedNoScratch)
    {
        TransitionProgramBuilder builder;
        builder.append_instruction(TransitionInstruction::transfer(
            TransitionLocation::stack(-1),
            TransitionLocation::register_file(0)));
        builder.emplace_resume_interpreter(TransitionLocation::stack(-1), 7);

        std::vector<TransitionInstruction> instructions =
            std::move(builder).finalize();

        EXPECT_EQ(0u, instructions.front().scratch_slot_count());
    }

    TEST(TransitionProgramBuilder, RejectsUninitializedScratchRead)
    {
        EXPECT_DEATH(
            {
                TransitionProgramBuilder builder;
                builder.emplace_transfer(TransitionLocation::stack(-1),
                                         TransitionLocation::scratch(0));
                builder.emplace_resume_interpreter(
                    TransitionLocation::stack(-1), 7);
                (void)std::move(builder).finalize();
            },
            "reads uninitialized scratch");
    }

    TEST(TransitionProgramBuilder, RequiresFinalTerminal)
    {
        EXPECT_DEATH(
            {
                TransitionProgramBuilder builder;
                builder.emplace_transfer(TransitionLocation::stack(-1),
                                         TransitionLocation::register_file(0));
                (void)std::move(builder).finalize();
            },
            "no final terminal instruction");
    }

    TEST(TransitionProgram, FormatsBodyPositions)
    {
        TransitionProgramBuilder builder;
        builder.emplace_transfer(TransitionLocation::scratch(0),
                                 TransitionLocation::register_file(1));
        builder.emplace_transfer(TransitionLocation::stack(-3),
                                 TransitionLocation::scratch(0));
        builder.emplace_resume_interpreter(TransitionLocation::stack(-3), 91);
        std::vector<TransitionInstruction> instructions =
            std::move(builder).finalize();

        EXPECT_EQ("transition {\n"
                  "  0: begin_transition {scratch_slots = 1}\n"
                  "  1: transfer scratch[0], register_file[1]\n"
                  "  2: transfer stack[-3], scratch[0]\n"
                  "  3: resume_interpreter stack[-3] "
                  "{resume_pc = 91}\n"
                  "}\n",
                  format_transition_program(instructions));
    }

}  // namespace cl::jit
