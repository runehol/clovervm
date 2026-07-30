#include "jit/allocation_materializer.h"

#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        constexpr PhysicalRegister x0(RegisterClass::GPR, 0);
        constexpr PhysicalRegister x1(RegisterClass::GPR, 1);
        constexpr PhysicalRegister x2(RegisterClass::GPR, 2);
        constexpr PhysicalRegister x3(RegisterClass::GPR, 3);

        LocationRequirement fixed(PhysicalLocation location)
        {
            return LocationRequirement::fixed(location);
        }

        AllocationConstraints constraints_with(
            std::vector<InstructionAllocationConstraints> overrides,
            bool include_second_scratch = true)
        {
            constexpr std::array registers = {x0, x1};
            constexpr std::array scratch_registers = {x2, x3};
            std::span<const PhysicalRegister> selected_scratch =
                include_second_scratch
                    ? std::span<const PhysicalRegister>(scratch_registers)
                    : std::span<const PhysicalRegister>(scratch_registers)
                          .first(1);
            std::vector<RegisterClassDefinition> definitions;
            definitions.emplace_back(RegisterClass::GPR, registers,
                                     selected_scratch);
            return AllocationConstraints(std::move(definitions),
                                         std::move(overrides));
        }

        RegisterAllocationResult
        allocate(const ControlFlowGraph &graph,
                 const AllocationConstraints &constraints,
                 PreparedAllocationProblem &prepared)
        {
            auto prepared_result =
                prepare_register_allocation(graph, constraints);
            EXPECT_TRUE(prepared_result);
            prepared = std::move(prepared_result).value();
            auto allocation_result = assign_bundles(prepared, constraints);
            EXPECT_TRUE(allocation_result);
            return std::move(allocation_result).value();
        }
    }  // namespace

    TEST(JitAllocationMaterializer, PublishesExistingValueLocations)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            parameter, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x0))});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {0, AccessTiming::Early, fixed(PhysicalLocation::reg(x0))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        EXPECT_EQ(x0, materialized.value()
                          .location_for(ProgramValueRef(parameter))
                          .reg());
        ASSERT_EQ(1u, entry->instructions().size());
        EXPECT_EQ(return_instruction, entry->instruction_at(0));
    }

    TEST(JitAllocationMaterializer, PublishesInstructionTemporaryLocations)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(parameter), TaggedValueRef(parameter));
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            parameter, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x0))});
        overrides.emplace_back(
            operation, std::vector<ProgramValueUseConstraint>{}, std::nullopt,
            std::vector<TemporaryConstraint>{TemporaryConstraint(
                LocationRequirement::fixed(PhysicalLocation::reg(x1)))});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {0, AccessTiming::Early, fixed(PhysicalLocation::reg(x0))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        EXPECT_EQ(x1, materialized.value().location_for(operation, 0).reg());
    }

    TEST(JitAllocationMaterializer, UsesGraphRewriterTraversalForEveryBlock)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction entry_parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {
            ProgramValueRef(entry_parameter)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction exit_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                exit, TaggedValueRef(exit_parameter));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            entry_parameter, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x0))});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {0, AccessTiming::Early, fixed(PhysicalLocation::reg(x0))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);
        ASSERT_TRUE(allocation.transfers().sets().empty());

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        EXPECT_EQ(x0, materialized.value()
                          .location_for(ProgramValueRef(entry_parameter))
                          .reg());
        EXPECT_EQ(x0, materialized.value()
                          .location_for(ProgramValueRef(exit_parameter))
                          .reg());
    }

    TEST(JitAllocationMaterializer,
         MaterializesBlockEdgeTransferAfterUnconditionalSource)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction value =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(value)};
        BlockEdge *edge = builder.make_block_edge(entry, exit, arguments);
        UnconditionalBranchInstruction old_branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        ParameterInstruction result =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                exit, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            value, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x0))});
        overrides.emplace_back(
            result, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x1))});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {ReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(PhysicalLocation::reg(x1))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        ASSERT_EQ(TransferPoint::Kind::BlockEdge,
                  allocation.transfers().sets().front().point.kind());

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(3u, graph->blocks().size());
        Block *split = graph->blocks()[1];
        EXPECT_EQ(entry, graph->blocks()[0]);
        EXPECT_EQ(exit, graph->blocks()[2]);
        ASSERT_EQ(1u, split->parameters().size());
        ASSERT_EQ(2u, split->instructions().size());
        MovInstruction move = split->instruction_at(0).as<MovInstruction>();
        EXPECT_EQ(split->parameter_at(0).id(), move.source().instruction_id());
        EXPECT_EQ(x0, materialized.value()
                          .location_for(ProgramValueRef(split->parameter_at(0)))
                          .reg());
        EXPECT_EQ(
            x1, materialized.value().location_for(ProgramValueRef(move)).reg());

        BlockEdge *incoming = entry->block_successor_edges().front();
        EXPECT_EQ(split, incoming->target());
        EXPECT_EQ(value.id(), incoming->arguments().front().instruction_id());
        BlockEdge *outgoing = split->block_successor_edges().front();
        EXPECT_EQ(exit, outgoing->target());
        EXPECT_EQ(move.id(), outgoing->arguments().front().instruction_id());
        EXPECT_TRUE(old_branch.is_poisoned());
    }

    TEST(JitAllocationMaterializer,
         MaterializesBlockEdgeTransferBeforeConditionalTarget)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *other = builder.emplace_block();
        Block *target = builder.emplace_block();
        ParameterInstruction condition =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction value =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(value)};
        BlockEdge *target_edge =
            builder.make_block_edge(entry, target, arguments);
        BlockEdge *other_edge =
            builder.make_block_edge(entry, other, arguments);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(condition), target_edge, other_edge);
        ParameterInstruction other_value =
            builder.emplace_parameter<ParameterInstruction>(other);
        ReturnInstruction other_return =
            builder.emplace_instruction<ReturnInstruction>(
                other, TaggedValueRef(other_value));
        ParameterInstruction target_value =
            builder.emplace_parameter<ParameterInstruction>(target);
        ReturnInstruction target_return =
            builder.emplace_instruction<ReturnInstruction>(
                target, TaggedValueRef(target_value));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            value, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x0))});
        overrides.emplace_back(
            other_value, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x0))});
        overrides.emplace_back(
            target_value, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x1))});
        overrides.emplace_back(
            other_return,
            std::vector<ProgramValueUseConstraint>{
                {ReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(PhysicalLocation::reg(x0))}});
        overrides.emplace_back(
            target_return,
            std::vector<ProgramValueUseConstraint>{
                {ReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(PhysicalLocation::reg(x1))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        EXPECT_EQ(target_edge,
                  allocation.transfers().sets().front().point.edge());

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(4u, graph->blocks().size());
        Block *split = graph->blocks()[2];
        EXPECT_EQ(entry, graph->blocks()[0]);
        EXPECT_EQ(other, graph->blocks()[1]);
        EXPECT_EQ(target, graph->blocks()[3]);
        EXPECT_EQ(split, entry->block_successor_edges()[0]->target());
        EXPECT_EQ(other, entry->block_successor_edges()[1]->target());
        ASSERT_EQ(2u, split->instructions().size());
        MovInstruction move = split->instruction_at(0).as<MovInstruction>();
        EXPECT_EQ(split->parameter_at(0).id(), move.source().instruction_id());
        EXPECT_EQ(target, split->block_successor_edges().front()->target());
        EXPECT_EQ(move.id(), split->block_successor_edges()
                                 .front()
                                 ->arguments()
                                 .front()
                                 .instruction_id());
    }

    TEST(JitAllocationMaterializer, InsertsSingletonStackToRegisterTransfer)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction old_return =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        StackLocation incoming(StackLocationKind::IncomingParameter, 4);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            parameter, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(incoming))});
        overrides.emplace_back(
            old_return,
            std::vector<ProgramValueUseConstraint>{
                {0, AccessTiming::Early, fixed(PhysicalLocation::reg(x0))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(2u, entry->instructions().size());
        LoadStackInstruction load =
            entry->instruction_at(0).as<LoadStackInstruction>();
        ReturnInstruction new_return =
            entry->instruction_at(1).as<ReturnInstruction>();
        EXPECT_EQ(parameter.id(), load.source().instruction_id());
        EXPECT_EQ(load.id(), new_return.return_value().instruction_id());
        EXPECT_TRUE(old_return.is_poisoned());
        EXPECT_EQ(4, materialized.value()
                         .location_for(ProgramValueRef(parameter))
                         .stack()
                         .frame_offset());
        EXPECT_EQ(
            x0, materialized.value().location_for(ProgramValueRef(load)).reg());
    }

    TEST(JitAllocationMaterializer, UsesActiveValueForSplitFromMergedBundle)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *middle = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ConstInstruction entry_value =
            builder.emplace_instruction<ConstInstruction>(entry,
                                                          Value::from_smi(0));
        std::array<ProgramValueRef, 1> entry_arguments = {
            ProgramValueRef(entry_value)};
        BlockEdge *entry_edge =
            builder.make_block_edge(entry, middle, entry_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    entry_edge);
        ParameterInstruction middle_value =
            builder.emplace_parameter<ParameterInstruction>(middle);
        std::array<ProgramValueRef, 1> middle_arguments = {
            ProgramValueRef(middle_value)};
        BlockEdge *middle_edge =
            builder.make_block_edge(middle, exit, middle_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            middle, middle_edge);
        ParameterInstruction exit_value =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ReturnInstruction old_return =
            builder.emplace_instruction<ReturnInstruction>(
                exit, TaggedValueRef(exit_value));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            entry_value, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x1))});
        overrides.emplace_back(
            middle_value, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x1))});
        overrides.emplace_back(
            exit_value, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x1))});
        overrides.emplace_back(
            old_return,
            std::vector<ProgramValueUseConstraint>{
                {ReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(PhysicalLocation::reg(x0))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        const BundleTransferSet &set = allocation.transfers().sets().front();
        EXPECT_EQ(TransferPoint::before_instruction(old_return), set.point);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(2u, exit->instructions().size());
        MovInstruction move = exit->instruction_at(0).as<MovInstruction>();
        ReturnInstruction new_return =
            exit->instruction_at(1).as<ReturnInstruction>();
        EXPECT_EQ(exit_value.id(), move.source().instruction_id());
        EXPECT_EQ(move.id(), new_return.return_value().instruction_id());
        EXPECT_TRUE(old_return.is_poisoned());
        EXPECT_EQ(x1, materialized.value()
                          .location_for(ProgramValueRef(exit_value))
                          .reg());
        EXPECT_EQ(
            x0, materialized.value().location_for(ProgramValueRef(move)).reg());
    }

    TEST(JitAllocationMaterializer, MaterializesPointerTransfers)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterPointerInstruction parameter =
            builder.emplace_parameter<ParameterPointerInstruction>(entry);
        MovPointerInstruction move =
            builder.emplace_instruction<MovPointerInstruction>(
                entry, PointerRef(parameter));
        TaggedValueRef result(builder.emplace_instruction<ConstInstruction>(
            entry, Value::None()));
        builder.emplace_instruction<ReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        StackLocation incoming(StackLocationKind::IncomingParameter, 4);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            parameter, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(incoming))});
        overrides.emplace_back(move, std::vector<ProgramValueUseConstraint>{
                                         {0, AccessTiming::Early,
                                          fixed(PhysicalLocation::reg(x0))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(4u, entry->instructions().size());
        LoadStackPointerInstruction load =
            entry->instruction_at(0).as<LoadStackPointerInstruction>();
        MovPointerInstruction rewritten_move =
            entry->instruction_at(1).as<MovPointerInstruction>();
        EXPECT_EQ(parameter.id(), load.source().instruction_id());
        EXPECT_EQ(load.id(), rewritten_move.source().instruction_id());
        EXPECT_EQ(
            x0, materialized.value().location_for(ProgramValueRef(load)).reg());
    }

    TEST(JitAllocationMaterializer, InsertsParallelStackToRegisterTransfers)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            lhs, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(StackLocation(
                                 StackLocationKind::IncomingParameter, 4)))});
        overrides.emplace_back(
            rhs, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(StackLocation(
                                 StackLocationKind::IncomingParameter, 3)))});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        ASSERT_EQ(2u, allocation.transfers().sets()[0].transfers.size());

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(4u, entry->instructions().size());
        LoadStackInstruction lhs_load =
            entry->instruction_at(0).as<LoadStackInstruction>();
        LoadStackInstruction rhs_load =
            entry->instruction_at(1).as<LoadStackInstruction>();
        AndSMIInstruction new_operation =
            entry->instruction_at(2).as<AndSMIInstruction>();
        EXPECT_EQ(lhs.id(), lhs_load.source().instruction_id());
        EXPECT_EQ(rhs.id(), rhs_load.source().instruction_id());
        EXPECT_EQ(lhs_load.id(), new_operation.lhs().instruction_id());
        EXPECT_EQ(rhs_load.id(), new_operation.rhs().instruction_id());
        EXPECT_TRUE(operation.is_poisoned());
        EXPECT_TRUE(return_instruction.is_poisoned());
    }

    TEST(JitAllocationMaterializer, InsertsRegisterToStackTransfer)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction old_return =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        StackLocation destination(StackLocationKind::LocalOrTemporary, -8);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            parameter, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x0))});
        overrides.emplace_back(
            old_return, std::vector<ProgramValueUseConstraint>{
                            {0, AccessTiming::Early,
                             fixed(PhysicalLocation::stack(destination))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(2u, entry->instructions().size());
        StoreStackInstruction store =
            entry->instruction_at(0).as<StoreStackInstruction>();
        ReturnInstruction new_return =
            entry->instruction_at(1).as<ReturnInstruction>();
        EXPECT_EQ(parameter.id(), store.source().instruction_id());
        EXPECT_EQ(store.id(), new_return.return_value().instruction_id());
        EXPECT_TRUE(old_return.is_poisoned());
        EXPECT_EQ(-8, materialized.value()
                          .location_for(ProgramValueRef(store))
                          .stack()
                          .frame_offset());
    }

    TEST(JitAllocationMaterializer, RoutesStackTransferThroughScratch)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction old_return =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        StackLocation source(StackLocationKind::IncomingParameter, 8);
        StackLocation destination(StackLocationKind::LocalOrTemporary, -8);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            parameter, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(source))});
        overrides.emplace_back(
            old_return, std::vector<ProgramValueUseConstraint>{
                            {0, AccessTiming::Early,
                             fixed(PhysicalLocation::stack(destination))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(3u, entry->instructions().size());
        LoadStackInstruction load =
            entry->instruction_at(0).as<LoadStackInstruction>();
        StoreStackInstruction store =
            entry->instruction_at(1).as<StoreStackInstruction>();
        ReturnInstruction new_return =
            entry->instruction_at(2).as<ReturnInstruction>();
        EXPECT_EQ(parameter.id(), load.source().instruction_id());
        EXPECT_EQ(load.id(), store.source().instruction_id());
        EXPECT_EQ(store.id(), new_return.return_value().instruction_id());
        EXPECT_EQ(
            x2, materialized.value().location_for(ProgramValueRef(load)).reg());
        EXPECT_EQ(-8, materialized.value()
                          .location_for(ProgramValueRef(store))
                          .stack()
                          .frame_offset());
    }

    TEST(JitAllocationMaterializer, ResolvesRegisterCycleWithScratch)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            lhs, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x0))});
        overrides.emplace_back(
            rhs, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::reg(x1))});
        overrides.emplace_back(
            operation,
            std::vector<ProgramValueUseConstraint>{
                {0, AccessTiming::Early, fixed(PhysicalLocation::reg(x1))},
                {1, AccessTiming::Early, fixed(PhysicalLocation::reg(x0))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(5u, entry->instructions().size());
        MovInstruction save = entry->instruction_at(0).as<MovInstruction>();
        MovInstruction move_rhs = entry->instruction_at(1).as<MovInstruction>();
        MovInstruction move_lhs = entry->instruction_at(2).as<MovInstruction>();
        AndSMIInstruction new_operation =
            entry->instruction_at(3).as<AndSMIInstruction>();
        EXPECT_EQ(lhs.id(), save.source().instruction_id());
        EXPECT_EQ(rhs.id(), move_rhs.source().instruction_id());
        EXPECT_EQ(save.id(), move_lhs.source().instruction_id());
        EXPECT_EQ(move_lhs.id(), new_operation.lhs().instruction_id());
        EXPECT_EQ(move_rhs.id(), new_operation.rhs().instruction_id());
        EXPECT_EQ(
            x2, materialized.value().location_for(ProgramValueRef(save)).reg());
        EXPECT_EQ(
            x0,
            materialized.value().location_for(ProgramValueRef(move_rhs)).reg());
        EXPECT_EQ(
            x1,
            materialized.value().location_for(ProgramValueRef(move_lhs)).reg());
    }

    TEST(JitAllocationMaterializer,
         ResolvesAllStackCycleWithTwoScratchRegisters)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        ReturnInstruction return_instruction =
            builder.emplace_instruction<ReturnInstruction>(
                entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        StackLocation first(StackLocationKind::IncomingParameter, 8);
        StackLocation second(StackLocationKind::IncomingParameter, 16);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            lhs, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(first))});
        overrides.emplace_back(
            rhs, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(second))});
        overrides.emplace_back(operation,
                               std::vector<ProgramValueUseConstraint>{
                                   {0, AccessTiming::Early,
                                    fixed(PhysicalLocation::stack(second))},
                                   {1, AccessTiming::Early,
                                    fixed(PhysicalLocation::stack(first))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides));
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(6u, entry->instructions().size());
        LoadStackInstruction save_lhs =
            entry->instruction_at(0).as<LoadStackInstruction>();
        LoadStackInstruction save_rhs =
            entry->instruction_at(1).as<LoadStackInstruction>();
        StoreStackInstruction move_rhs =
            entry->instruction_at(2).as<StoreStackInstruction>();
        StoreStackInstruction move_lhs =
            entry->instruction_at(3).as<StoreStackInstruction>();
        AndSMIInstruction new_operation =
            entry->instruction_at(4).as<AndSMIInstruction>();
        EXPECT_EQ(lhs.id(), save_lhs.source().instruction_id());
        EXPECT_EQ(rhs.id(), save_rhs.source().instruction_id());
        EXPECT_EQ(save_rhs.id(), move_rhs.source().instruction_id());
        EXPECT_EQ(save_lhs.id(), move_lhs.source().instruction_id());
        EXPECT_EQ(move_lhs.id(), new_operation.lhs().instruction_id());
        EXPECT_EQ(move_rhs.id(), new_operation.rhs().instruction_id());
        EXPECT_EQ(
            x2,
            materialized.value().location_for(ProgramValueRef(save_lhs)).reg());
        EXPECT_EQ(
            x3,
            materialized.value().location_for(ProgramValueRef(save_rhs)).reg());
        EXPECT_EQ(8, materialized.value()
                         .location_for(ProgramValueRef(move_rhs))
                         .stack()
                         .frame_offset());
        EXPECT_EQ(16, materialized.value()
                          .location_for(ProgramValueRef(move_lhs))
                          .stack()
                          .frame_offset());
        EXPECT_TRUE(operation.is_poisoned());
        EXPECT_TRUE(return_instruction.is_poisoned());
    }

    TEST(JitAllocationMaterializer,
         ReportsAllStackCycleWithoutSecondScratchRegister)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        builder.emplace_instruction<ReturnInstruction>(
            entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        StackLocation first(StackLocationKind::IncomingParameter, 8);
        StackLocation second(StackLocationKind::IncomingParameter, 16);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            lhs, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(first))});
        overrides.emplace_back(
            rhs, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             fixed(PhysicalLocation::stack(second))});
        overrides.emplace_back(operation,
                               std::vector<ProgramValueUseConstraint>{
                                   {0, AccessTiming::Early,
                                    fixed(PhysicalLocation::stack(second))},
                                   {1, AccessTiming::Early,
                                    fixed(PhysicalLocation::stack(first))}});
        AllocationConstraints constraints =
            constraints_with(std::move(overrides), false);
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized.has_error());
        EXPECT_EQ(RegisterAllocationError::InsufficientTransferScratchRegisters,
                  materialized.error());
    }

}  // namespace cl::jit
