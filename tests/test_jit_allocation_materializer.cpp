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

        LocationRequirement fixed(PhysicalLocation location)
        {
            return LocationRequirement::fixed(location);
        }

        AllocationConstraints constraints_with(
            std::vector<InstructionAllocationConstraints> overrides)
        {
            constexpr std::array registers = {x0, x1};
            std::vector<RegisterClassDefinition> definitions;
            definitions.emplace_back(RegisterClass::GPR, registers, x2);
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
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction *return_instruction =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        EXPECT_EQ(x0, materialized.value()
                          .location_for(ProgramValueRef(parameter))
                          .reg());
        ASSERT_EQ(1u, entry->instructions().size());
        EXPECT_EQ(return_instruction, entry->instructions().front());
    }

    TEST(JitAllocationMaterializer, PublishesInstructionTemporaryLocations)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction *operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(parameter), TaggedValueRef(parameter));
        ReturnInstruction *return_instruction =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
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
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction *entry_parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> arguments = {
            ProgramValueRef(entry_parameter)};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction *exit_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        ReturnInstruction *return_instruction =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
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

    TEST(JitAllocationMaterializer, InsertsSingletonStackToRegisterTransfer)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction *old_return =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(2u, entry->instructions().size());
        LoadStackInstruction *load =
            entry->instructions()[0]->as<LoadStackInstruction>();
        ReturnInstruction *new_return =
            entry->instructions()[1]->as<ReturnInstruction>();
        EXPECT_EQ(parameter->id(), load->source().instruction_id());
        EXPECT_EQ(load->id(), new_return->return_value().instruction_id());
        EXPECT_TRUE(old_return->is_detached());
        EXPECT_EQ(4, materialized.value()
                         .location_for(ProgramValueRef(parameter))
                         .stack()
                         .frame_offset());
        EXPECT_EQ(
            x0, materialized.value().location_for(ProgramValueRef(load)).reg());
    }

    TEST(JitAllocationMaterializer, InsertsParallelStackToRegisterTransfers)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction *rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction *operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        ReturnInstruction *return_instruction =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        ASSERT_EQ(2u, allocation.transfers().sets()[0].transfers.size());

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(4u, entry->instructions().size());
        LoadStackInstruction *lhs_load =
            entry->instructions()[0]->as<LoadStackInstruction>();
        LoadStackInstruction *rhs_load =
            entry->instructions()[1]->as<LoadStackInstruction>();
        AndSMIInstruction *new_operation =
            entry->instructions()[2]->as<AndSMIInstruction>();
        EXPECT_EQ(lhs->id(), lhs_load->source().instruction_id());
        EXPECT_EQ(rhs->id(), rhs_load->source().instruction_id());
        EXPECT_EQ(lhs_load->id(), new_operation->lhs().instruction_id());
        EXPECT_EQ(rhs_load->id(), new_operation->rhs().instruction_id());
        EXPECT_TRUE(operation->is_detached());
        EXPECT_TRUE(return_instruction->is_detached());
    }

    TEST(JitAllocationMaterializer, InsertsRegisterToStackTransfer)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction *old_return =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(2u, entry->instructions().size());
        StoreStackInstruction *store =
            entry->instructions()[0]->as<StoreStackInstruction>();
        ReturnInstruction *new_return =
            entry->instructions()[1]->as<ReturnInstruction>();
        EXPECT_EQ(parameter->id(), store->source().instruction_id());
        EXPECT_EQ(store->id(), new_return->return_value().instruction_id());
        EXPECT_TRUE(old_return->is_detached());
        EXPECT_EQ(-8, materialized.value()
                          .location_for(ProgramValueRef(store))
                          .stack()
                          .frame_offset());
    }

    TEST(JitAllocationMaterializer, RoutesStackTransferThroughScratch)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ReturnInstruction *old_return =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(3u, entry->instructions().size());
        LoadStackInstruction *load =
            entry->instructions()[0]->as<LoadStackInstruction>();
        StoreStackInstruction *store =
            entry->instructions()[1]->as<StoreStackInstruction>();
        ReturnInstruction *new_return =
            entry->instructions()[2]->as<ReturnInstruction>();
        EXPECT_EQ(parameter->id(), load->source().instruction_id());
        EXPECT_EQ(load->id(), store->source().instruction_id());
        EXPECT_EQ(store->id(), new_return->return_value().instruction_id());
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
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction *rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction *operation =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized);
        ASSERT_EQ(5u, entry->instructions().size());
        MovInstruction *save = entry->instructions()[0]->as<MovInstruction>();
        MovInstruction *move_rhs =
            entry->instructions()[1]->as<MovInstruction>();
        MovInstruction *move_lhs =
            entry->instructions()[2]->as<MovInstruction>();
        AndSMIInstruction *new_operation =
            entry->instructions()[3]->as<AndSMIInstruction>();
        EXPECT_EQ(lhs->id(), save->source().instruction_id());
        EXPECT_EQ(rhs->id(), move_rhs->source().instruction_id());
        EXPECT_EQ(save->id(), move_lhs->source().instruction_id());
        EXPECT_EQ(move_lhs->id(), new_operation->lhs().instruction_id());
        EXPECT_EQ(move_rhs->id(), new_operation->rhs().instruction_id());
        EXPECT_EQ(
            x2, materialized.value().location_for(ProgramValueRef(save)).reg());
        EXPECT_EQ(
            x0,
            materialized.value().location_for(ProgramValueRef(move_rhs)).reg());
        EXPECT_EQ(
            x1,
            materialized.value().location_for(ProgramValueRef(move_lhs)).reg());
    }

    TEST(JitAllocationMaterializer, ReportsAllStackCycleBeforeRewriting)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction *rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction *operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        ReturnInstruction *return_instruction =
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
        PreparedAllocationProblem prepared({}, {}, {}, {}, {}, {});
        RegisterAllocationResult allocation =
            allocate(*graph, constraints, prepared);

        auto materialized = materialize_allocation(session, *graph, prepared,
                                                   constraints, allocation);

        ASSERT_TRUE(materialized.has_error());
        EXPECT_EQ(RegisterAllocationError::RequiresTransferSpillSlot,
                  materialized.error());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(operation, entry->instructions()[0]);
        EXPECT_EQ(return_instruction, entry->instructions()[1]);
    }

}  // namespace cl::jit
