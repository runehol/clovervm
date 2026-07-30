#include "jit/dead_code_elimination.h"

#include "jit/compilation_session.h"
#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <utility>

namespace cl::jit
{
    TEST(JitDeadCodeElimination, EliminatesUnusedEffectFreeInstructions)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ConstInstruction unused =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        MovInstruction unused_copy =
            builder.emplace_instruction<MovInstruction>(entry,
                                                        TaggedValueRef(unused));
        ConstInstruction result =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        auto elimination = eliminate_dead_code(session, *graph);

        ASSERT_TRUE(elimination);
        EXPECT_TRUE(std::move(elimination).value());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(result, entry->instruction_at(0));
        EXPECT_EQ(InstructionKind::Return, entry->instruction_at(1).kind());
        EXPECT_TRUE(unused.is_poisoned());
        EXPECT_TRUE(unused_copy.is_poisoned());
    }

    TEST(JitDeadCodeElimination, RetainsValuesPassedAcrossBlockEdges)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ConstInstruction argument =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(argument)};
        BlockEdge *edge = builder.make_block_edge(entry, exit, arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto elimination = eliminate_dead_code(session, *graph);

        ASSERT_TRUE(elimination);
        EXPECT_FALSE(std::move(elimination).value());
        EXPECT_FALSE(argument.is_poisoned());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(argument, entry->instruction_at(0));
    }

    TEST(JitDeadCodeElimination, RemovesDeadBlockParametersAndTheirArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ConstInstruction dead_argument =
            builder.emplace_instruction<ConstInstruction>(entry,
                                                          Value::False());
        ConstInstruction live_argument =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        std::array<ProgramValueRef, 2> arguments = {
            ProgramValueRef(dead_argument), ProgramValueRef(live_argument)};
        BlockEdge *old_edge = builder.make_block_edge(entry, exit, arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    old_edge);
        ParameterInstruction dead_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ParameterInstruction live_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(live_parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto elimination = eliminate_dead_code(session, *graph);

        ASSERT_TRUE(elimination);
        EXPECT_TRUE(std::move(elimination).value());
        EXPECT_TRUE(dead_argument.is_poisoned());
        EXPECT_TRUE(dead_parameter.is_poisoned());
        EXPECT_FALSE(live_argument.is_poisoned());
        EXPECT_FALSE(live_parameter.is_poisoned());
        ASSERT_EQ(1u, exit->parameters().size());
        EXPECT_EQ(live_parameter, exit->parameter_at(0));
        BlockEdge *new_edge = entry->block_successor_edges()[0];
        EXPECT_NE(old_edge, new_edge);
        ASSERT_EQ(1u, new_edge->arguments().size());
        EXPECT_EQ(live_argument.id(),
                  new_edge->arguments()[0].instruction_id());
    }

    TEST(JitDeadCodeElimination, EliminatesUnusedDeoptimizingInstructions)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(parameter)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(entry, captured,
                                                             BytecodePC{7});
        AddSMIInstruction unused_add =
            builder.emplace_instruction<AddSMIInstruction>(
                entry, TaggedValueRef(parameter), TaggedValueRef(parameter),
                SnapshotRef(snapshot));
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto elimination = eliminate_dead_code(session, *graph);

        ASSERT_TRUE(elimination);
        EXPECT_TRUE(std::move(elimination).value());
        EXPECT_TRUE(snapshot.is_poisoned());
        EXPECT_TRUE(unused_add.is_poisoned());
        ASSERT_EQ(1u, entry->instructions().size());
        EXPECT_EQ(InstructionKind::Return, entry->instruction_at(0).kind());
    }

    TEST(JitDeadCodeElimination, EliminatesUnusedAllocations)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction result =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterF64Instruction value =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction unused_box =
            builder.emplace_instruction<BoxF64Instruction>(entry,
                                                           F64Ref(value));
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        auto elimination = eliminate_dead_code(session, *graph);

        ASSERT_TRUE(elimination);
        EXPECT_TRUE(std::move(elimination).value());
        EXPECT_TRUE(unused_box.is_poisoned());
        ASSERT_EQ(1u, entry->instructions().size());
    }

    TEST(JitDeadCodeElimination, RetainsUnusedControlFlowInstructions)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePC{7});
        CheckNotImplementedInstruction check =
            builder.emplace_instruction<CheckNotImplementedInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot));
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto elimination = eliminate_dead_code(session, *graph);

        ASSERT_TRUE(elimination);
        EXPECT_FALSE(std::move(elimination).value());
        EXPECT_FALSE(snapshot.is_poisoned());
        EXPECT_FALSE(check.is_poisoned());
        ASSERT_EQ(3u, entry->instructions().size());
    }

    TEST(JitDeadCodeElimination, RetainsUnusedPythonCalls)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePC{7});
        PythonCallInstruction unused_call =
            builder.emplace_instruction<PythonCallInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot),
                std::span<const TaggedValueRef>{}, BytecodePC{7});
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto elimination = eliminate_dead_code(session, *graph);

        ASSERT_TRUE(elimination);
        EXPECT_FALSE(std::move(elimination).value());
        EXPECT_FALSE(snapshot.is_poisoned());
        EXPECT_FALSE(unused_call.is_poisoned());
        ASSERT_EQ(3u, entry->instructions().size());
    }

    TEST(JitDeadCodeElimination, RetainsSideExitArguments)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction input =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction region_parameter =
            builder.make_instruction<ParameterInstruction>();
        std::array<ProgramValueRef, 1> snapshot_values = {
            ProgramValueRef(region_parameter)};
        ExitToInterpreterInstruction exit =
            builder.make_instruction<ExitToInterpreterInstruction>(
                snapshot_values, BytecodePC{7});
        std::array<InstructionId, 1> parameter_ids = {region_parameter.id()};
        std::array<InstructionId, 1> instructions = {exit.id()};
        std::array<ProgramValueRef, 1> inputs = {ProgramValueRef(input)};
        SideExitRegionId region =
            builder.make_side_exit_region(parameter_ids, instructions)->id();
        builder.emplace_instruction<ResumeInInterpreterWithSideExitInstruction>(
            entry, inputs, region);
        ControlFlowGraph *graph = builder.finalize();

        auto elimination = eliminate_dead_code(session, *graph);

        ASSERT_TRUE(elimination);
        EXPECT_FALSE(std::move(elimination).value());
        EXPECT_FALSE(input.is_poisoned());
        ASSERT_EQ(1u, entry->parameters().size());
    }

}  // namespace cl::jit
