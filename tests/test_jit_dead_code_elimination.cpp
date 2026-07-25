#include "jit/dead_code_elimination.h"

#include "jit/compilation_session.h"
#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <array>

namespace cl::jit
{
    TEST(JitDeadCodeElimination, EliminatesUnusedEffectFreeInstructions)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ConstInstruction *unused =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        MovInstruction *unused_copy =
            builder.emplace_instruction<MovInstruction>(entry,
                                                        TaggedValueRef(unused));
        ConstInstruction *result =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        eliminate_dead_code(session, *graph);

        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(result, entry->instructions()[0]);
        EXPECT_EQ(InstructionKind::Return, entry->instructions()[1]->kind());
        EXPECT_TRUE(unused->is_detached());
        EXPECT_TRUE(unused_copy->is_detached());
    }

    TEST(JitDeadCodeElimination, RetainsValuesPassedAcrossBlockEdges)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ConstInstruction *argument =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(argument)};
        BlockEdge *edge = builder.make_block_edge(entry, exit, arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<ReturnInstruction>(
            exit, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        eliminate_dead_code(session, *graph);

        EXPECT_FALSE(argument->is_detached());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(argument, entry->instructions()[0]);
    }

    TEST(JitDeadCodeElimination, RetainsUnusedEffectfulInstructions)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(parameter)};
        SnapshotInstruction *snapshot =
            builder.emplace_instruction<SnapshotInstruction>(entry, captured,
                                                             BytecodePC{7});
        AddSMIInstruction *unused_add =
            builder.emplace_instruction<AddSMIInstruction>(
                entry, TaggedValueRef(parameter), TaggedValueRef(parameter),
                SnapshotRef(snapshot));
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        eliminate_dead_code(session, *graph);

        EXPECT_FALSE(snapshot->is_detached());
        EXPECT_FALSE(unused_add->is_detached());
        ASSERT_EQ(3u, entry->instructions().size());
    }

}  // namespace cl::jit
