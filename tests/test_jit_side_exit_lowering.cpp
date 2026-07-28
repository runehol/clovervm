#include "jit/side_exit_lowering.h"

#include "jit/compilation_session.h"
#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace cl::jit
{
    TEST(JitSideExitLowering,
         RetainsSunkInstructionsInProgramOrderAndBindsTheirInputs)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction second =
            builder.emplace_parameter<ParameterInstruction>(entry);
        MovInstruction first_move = builder.emplace_instruction<MovInstruction>(
            entry, TaggedValueRef(first));
        MovInstruction second_move =
            builder.emplace_instruction<MovInstruction>(entry,
                                                        TaggedValueRef(second));
        std::array<ProgramValueRef, 3> captured = {ProgramValueRef(second_move),
                                                   ProgramValueRef(first_move),
                                                   ProgramValueRef(second)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(entry, captured,
                                                             BytecodePC{17});
        ResumeInInterpreterInstruction old_owner =
            builder.emplace_instruction<ResumeInInterpreterInstruction>(
                entry, SnapshotRef(snapshot));
        ControlFlowGraph *graph = builder.finalize();

        SunkInstructionIds sunk = sink_snapshots(*graph);
        ASSERT_EQ(1u, sunk.size());
        EXPECT_TRUE(sunk.contains(snapshot.id()));
        sunk.insert(first_move.id());
        sunk.insert(second_move.id());

        auto lowered = lower_side_exits(session, *graph, sunk);

        ASSERT_TRUE(lowered);
        EXPECT_TRUE(std::move(lowered).value());
        EXPECT_EQ(IRLevel::Machine, graph->ir_level());
        ASSERT_EQ(1u, entry->instructions().size());
        auto owner = entry->instruction_at(0)
                         .as<ResumeInInterpreterWithSideExitInstruction>();
        EXPECT_TRUE(old_owner.is_poisoned());
        EXPECT_FALSE(first_move.is_poisoned());
        EXPECT_FALSE(second_move.is_poisoned());
        EXPECT_FALSE(snapshot.is_poisoned());

        ASSERT_EQ(2u, owner.side_exit_arguments().size());
        EXPECT_EQ(first.id(), owner.side_exit_arguments()[0].instruction_id());
        EXPECT_EQ(second.id(), owner.side_exit_arguments()[1].instruction_id());

        ASSERT_EQ(1u, graph->side_exits().size());
        const SideExit &side_exit = graph->side_exit(owner.side_exit());
        ASSERT_EQ(2u, side_exit.inputs().size());
        EXPECT_EQ(first.id(), side_exit.inputs()[0].instruction_id());
        EXPECT_EQ(second.id(), side_exit.inputs()[1].instruction_id());
        ASSERT_EQ(3u, side_exit.instructions().size());
        EXPECT_EQ(first_move.id(), side_exit.instructions()[0]);
        EXPECT_EQ(second_move.id(), side_exit.instructions()[1]);
        EXPECT_EQ(snapshot.id(), side_exit.instructions()[2]);
    }

    TEST(JitSideExitLowering, RejectsASelectedSnapshotWithAnExecutableUse)
    {
        EXPECT_DEATH(
            ([] {
                CompilationSession session;
                GraphBuilder builder(session);
                Block *entry = builder.emplace_block();
                ParameterInstruction parameter =
                    builder.emplace_parameter<ParameterInstruction>(entry);
                std::array<ProgramValueRef, 1> captured = {
                    ProgramValueRef(parameter)};
                SnapshotInstruction snapshot =
                    builder.emplace_instruction<SnapshotInstruction>(
                        entry, captured, BytecodePC{23});
                builder.emplace_instruction<CheckNotImplementedInstruction>(
                    entry, TaggedValueRef(parameter), SnapshotRef(snapshot));
                builder.emplace_instruction<ReturnInstruction>(
                    entry, TaggedValueRef(parameter));
                ControlFlowGraph *graph = builder.finalize();

                SunkInstructionIds sunk = sink_snapshots(*graph);
                (void)lower_side_exits(session, *graph, sunk);
            }()),
            "not retained by a side exit");
    }

}  // namespace cl::jit
