#include "jit/block_parameter_join.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "object_model/value.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace cl::jit
{
    TEST(JitBlockParameterJoin, OrdersJoinsByDestinationParameter)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();

        ConstInstruction first =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        ConstInstruction second = builder.emplace_instruction<ConstInstruction>(
            entry, Value::False());
        std::array<ProgramValueRef, 2> arguments = {ProgramValueRef(first),
                                                    ProgramValueRef(second)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            entry, builder.make_block_edge(entry, exit, arguments));
        ParameterInstruction first_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ParameterInstruction second_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<BareReturnInstruction>(
            exit, TaggedValueRef(first_parameter));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionId> parameters;
        for(BlockParameterJoin join: graph->block_parameter_joins(*exit))
        {
            parameters.push_back(join.parameter().id());
        }
        EXPECT_EQ((std::vector<InstructionId>{first_parameter.id(),
                                              second_parameter.id()}),
                  parameters);
    }

    TEST(JitBlockParameterJoin, ExposesEveryIncomingEdgeAndItsParameterArgument)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *loop = builder.emplace_block();

        ConstInstruction first =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        ConstInstruction second = builder.emplace_instruction<ConstInstruction>(
            entry, Value::False());
        std::array<ProgramValueRef, 1> first_arguments = {
            ProgramValueRef(first)};
        std::array<ProgramValueRef, 1> second_arguments = {
            ProgramValueRef(second)};
        BlockEdge *first_edge =
            builder.make_block_edge(entry, loop, first_arguments);
        BlockEdge *second_edge =
            builder.make_block_edge(entry, loop, second_arguments);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(first), first_edge, second_edge);

        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(loop);
        std::array<ProgramValueRef, 1> backedge_arguments = {
            ProgramValueRef(parameter)};
        BlockEdge *backedge =
            builder.make_block_edge(loop, loop, backedge_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(loop,
                                                                    backedge);
        ControlFlowGraph *graph = builder.finalize();

        auto joins = graph->block_parameter_joins(*loop);
        ASSERT_EQ(1u, joins.size());
        BlockParameterJoin join = *joins.begin();
        EXPECT_EQ(loop, &join.block());
        EXPECT_EQ(parameter, join.parameter());
        EXPECT_EQ(first.id(),
                  join.argument_from(first_edge->id()).instruction_id());
        EXPECT_EQ(second.id(),
                  join.argument_from(second_edge->id()).instruction_id());
        EXPECT_EQ(parameter.id(),
                  join.argument_from(backedge->id()).instruction_id());

        std::vector<BlockEdgeId> incoming_edges;
        std::vector<InstructionId> incoming_values;
        for(IncomingArgument incoming: join.incoming_arguments())
        {
            incoming_edges.push_back(incoming.edge);
            incoming_values.push_back(incoming.value.instruction_id());
        }
        EXPECT_EQ((std::vector<BlockEdgeId>{first_edge->id(), second_edge->id(),
                                            backedge->id()}),
                  incoming_edges);
        EXPECT_EQ((std::vector<InstructionId>{first.id(), second.id(),
                                              parameter.id()}),
                  incoming_values);
    }

}  // namespace cl::jit
