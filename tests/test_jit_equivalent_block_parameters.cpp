#include "jit/equivalent_block_parameters.h"

#include "jit/compilation_session.h"
#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace cl::jit
{
    TEST(JitEquivalentBlockParameters, CollapsesLoopParametersWithOneBase)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *loop = builder.emplace_block();

        ConstInstruction base =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        std::array<ProgramValueRef, 2> entry_arguments = {
            ProgramValueRef(base), ProgramValueRef(base)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            entry, builder.make_block_edge(entry, loop, entry_arguments));

        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(loop);
        ParameterInstruction second =
            builder.emplace_parameter<ParameterInstruction>(loop);
        std::array<ProgramValueRef, 2> captured = {ProgramValueRef(first),
                                                   ProgramValueRef(second)};
        builder.emplace_instruction<SnapshotInstruction>(loop, captured,
                                                         BytecodePC{7});
        std::array<ProgramValueRef, 2> backedge_arguments = {
            ProgramValueRef(first), ProgramValueRef(second)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            loop, builder.make_block_edge(loop, loop, backedge_arguments));
        ControlFlowGraph *graph = builder.finalize();

        auto collapse = collapse_equivalent_block_parameters(session, *graph);

        ASSERT_TRUE(collapse);
        EXPECT_TRUE(std::move(collapse).value());
        ASSERT_EQ(1u, loop->parameters().size());
        EXPECT_EQ(first.id(), loop->parameter_at(0).id());
        EXPECT_TRUE(second.is_poisoned());

        BlockEdge *entry_edge = entry->block_successor_edges()[0];
        ASSERT_EQ(1u, entry_edge->arguments().size());
        EXPECT_EQ(base.id(), entry_edge->arguments()[0].instruction_id());

        SnapshotInstruction snapshot =
            loop->instruction_at(0).as<SnapshotInstruction>();
        ASSERT_EQ(2u, snapshot.captured_values().size());
        EXPECT_EQ(first.id(), snapshot.captured_values()[0].instruction_id());
        EXPECT_EQ(first.id(), snapshot.captured_values()[1].instruction_id());

        BlockEdge *backedge = loop->block_successor_edges()[0];
        ASSERT_EQ(1u, backedge->arguments().size());
        EXPECT_EQ(first.id(), backedge->arguments()[0].instruction_id());
    }

}  // namespace cl::jit
