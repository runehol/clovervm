#include "jit/transition_program.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
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
            TransitionLocation::register_file(1),
            TransitionLocation::stack(-4));
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
            TransitionLocation::register_file(0),
            TransitionLocation::scratch(3)));
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

}  // namespace cl::jit
