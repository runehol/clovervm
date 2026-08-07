#include "jit/register_allocator.h"
#include "test_helpers.h"

#include "jit/aarch64_allocation_constraints.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "jit/side_exit_lowering.h"
#include "runtime/trusted_handler.h"

#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        constexpr PhysicalRegister x0(RegisterClass::GPR, 0);
        constexpr PhysicalRegister x1(RegisterClass::GPR, 1);
        constexpr PhysicalRegister x2(RegisterClass::GPR, 2);
        constexpr PhysicalRegister v0(RegisterClass::SIMD, 0);
        constexpr PhysicalRegister v1(RegisterClass::SIMD, 1);

        Value allocator_test_trusted_handler(ThreadState *, Value value)
        {
            return value;
        }

        TrustedHandlerTarget allocator_test_trusted_target()
        {
            return erase_trusted_handler_target(allocator_test_trusted_handler);
        }

        LocationRequirement fixed(PhysicalRegister reg)
        {
            return LocationRequirement::fixed(PhysicalLocation::reg(reg));
        }

        TaggedValueRef emplace_constant(GraphBuilder &builder, Block *block,
                                        Value value)
        {
            return TaggedValueRef(
                builder.emplace_instruction<ConstInstruction>(block, value));
        }

        std::vector<RegisterClassDefinition> gpr_definition()
        {
            constexpr std::array registers = {x0, x1, x2};
            std::vector<RegisterClassDefinition> result;
            result.emplace_back(RegisterClass::GPR, registers);
            return result;
        }

        AllocationConstraints gpr_constraints(
            std::span<const PhysicalRegister> registers,
            std::vector<InstructionAllocationConstraints> overrides = {})
        {
            std::vector<RegisterClassDefinition> definitions;
            definitions.emplace_back(RegisterClass::GPR, registers);
            return AllocationConstraints(std::move(definitions),
                                         std::move(overrides));
        }

        InstructionAllocationConstraints
        trusted_call_constraints(TrustedHandlerCallInstruction call)
        {
            RegisterSet clobbers;
            clobbers.insert(x1);
            return InstructionAllocationConstraints(
                call,
                {{TrustedHandlerCallInstruction::arguments_operand_index,
                  AccessTiming::Early,
                  LocationRequirement::fixed_operand_copy(x1)}},
                ResultConstraint{AccessTiming::Late, fixed(x0)}, {}, clobbers,
                CallLocalSpillPolicy::Allow);
        }

        AllocationConstraints allocator_test_constraints(
            std::initializer_list<Instruction> entry_parameters,
            BareReturnInstruction return_instruction,
            std::optional<Instruction> instruction_with_temporary =
                std::nullopt)
        {
            std::vector<InstructionAllocationConstraints> overrides;
            size_t parameter_index = 0;
            for(Instruction parameter: entry_parameters)
            {
                overrides.emplace_back(
                    parameter, std::vector<ProgramValueUseConstraint>{},
                    ResultConstraint{
                        AccessTiming::Late,
                        fixed(PhysicalRegister(RegisterClass::GPR,
                                               parameter_index++))});
            }
            overrides.emplace_back(
                return_instruction,
                std::vector<ProgramValueUseConstraint>{
                    {BareReturnInstruction::return_value_operand_index,
                     AccessTiming::Early, fixed(x0)}});
            if(instruction_with_temporary.has_value())
            {
                overrides.emplace_back(
                    *instruction_with_temporary,
                    std::vector<ProgramValueUseConstraint>{}, std::nullopt,
                    std::vector<TemporaryConstraint>{
                        TemporaryConstraint(LocationRequirement::any_register(
                            RegisterClass::GPR))});
            }
            return AllocationConstraints(gpr_definition(),
                                         std::move(overrides));
        }
    }  // namespace

    TEST(JitRegisterAllocator, ComputesMinimumInstructionLivenessCoverage)
    {
        LivenessPosition early(10);

        LivenessRange early_use = minimum_liveness_coverage(
            early, OccurrenceKind::Use, AccessTiming::Early);
        EXPECT_EQ(10u, early_use.start.value());
        EXPECT_EQ(11u, early_use.end.value());

        LivenessRange late_use = minimum_liveness_coverage(
            early, OccurrenceKind::Use, AccessTiming::Late);
        EXPECT_EQ(10u, late_use.start.value());
        EXPECT_EQ(12u, late_use.end.value());

        LivenessRange early_def = minimum_liveness_coverage(
            early, OccurrenceKind::Def, AccessTiming::Early);
        EXPECT_EQ(10u, early_def.start.value());
        EXPECT_EQ(12u, early_def.end.value());

        LivenessRange late_def = minimum_liveness_coverage(
            early, OccurrenceKind::Def, AccessTiming::Late);
        EXPECT_EQ(11u, late_def.start.value());
        EXPECT_EQ(12u, late_def.end.value());
    }

    TEST(JitRegisterAllocator, SortsLiveRangeReferencesByPosition)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(parameter), TaggedValueRef(parameter));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(operation,
                               std::vector<ProgramValueUseConstraint>{
                                   {0, AccessTiming::Late, fixed(x0)},
                                   {1, AccessTiming::Early, fixed(x0)}});
        constexpr std::array registers = {x0};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);

        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        const LiveRange &range = prepared.live_ranges().front();
        ASSERT_EQ(3u, range.occurrences.size());
        EXPECT_EQ(
            LivenessPosition(1),
            prepared.occurrences()[range.occurrences[0].value()].position);
        EXPECT_EQ(
            LivenessPosition(2),
            prepared.occurrences()[range.occurrences[1].value()].position);
        EXPECT_EQ(
            LivenessPosition(3),
            prepared.occurrences()[range.occurrences[2].value()].position);
        ASSERT_EQ(2u, range.fixed_constraints.size());
        EXPECT_EQ(
            LivenessPosition(2),
            prepared.fixed_constraints()[range.fixed_constraints[0].value()]
                .position);
        EXPECT_EQ(
            LivenessPosition(3),
            prepared.fixed_constraints()[range.fixed_constraints[1].value()]
                .position);
    }

    TEST(JitRegisterAllocator, AllocatesAndMaterializesGraph)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();
        AllocationConstraints constraints =
            allocator_test_constraints({parameter}, return_instruction);

        auto allocation = allocate_registers(session, *graph, constraints);

        ASSERT_TRUE(allocation);
        EXPECT_EQ(PhysicalLocation::reg(x0),
                  allocation.value().locations().location_for(
                      ProgramValueRef(parameter)));
    }

    TEST(JitRegisterAllocator,
         MaterializesSideExitArgumentsWithoutRewritingSideExitInputs)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(parameter)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, captured, BytecodePCOffset{7});
        builder.emplace_instruction<ResumeInInterpreterInstruction>(
            entry, SnapshotRef(snapshot));
        ControlFlowGraph *graph = builder.finalize();

        SunkInstructionIds sunk = sink_snapshots(*graph);
        auto lowering = lower_side_exits(session, *graph, sunk);
        ASSERT_TRUE(lowering);
        ASSERT_TRUE(std::move(lowering).value());

        ASSERT_EQ(1u, entry->instructions().size());
        ResumeInInterpreterWithSideExitInstruction owner =
            entry->instruction_at(0)
                .as<ResumeInInterpreterWithSideExitInstruction>();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(owner,
                               std::vector<ProgramValueUseConstraint>{
                                   {ResumeInInterpreterWithSideExitInstruction::
                                        side_exit_arguments_operand_index,
                                    AccessTiming::Late, fixed(x1)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto allocation = allocate_registers(session, *graph, constraints);

        ASSERT_TRUE(allocation);
        LocationAssignments locations =
            std::move(allocation).value().take_locations();
        ASSERT_EQ(2u, entry->instructions().size());
        MovInstruction move = entry->instruction_at(0).as<MovInstruction>();
        auto rewritten_owner =
            entry->instruction_at(1)
                .as<ResumeInInterpreterWithSideExitInstruction>();
        ASSERT_EQ(1u, rewritten_owner.side_exit_arguments().size());
        EXPECT_EQ(move.id(),
                  rewritten_owner.side_exit_arguments()[0].instruction_id());
        EXPECT_EQ(parameter.id(), move.source().instruction_id());
        EXPECT_EQ(PhysicalLocation::reg(x0),
                  locations.location_for(ProgramValueRef(parameter)));
        EXPECT_EQ(PhysicalLocation::reg(x1),
                  locations.location_for(ProgramValueRef(move)));

        ExitToInterpreterInstruction retained_exit =
            graph->storage()
                ->side_exit_region(rewritten_owner.side_exit_region())
                .instruction_at(0)
                .as<ExitToInterpreterInstruction>();
        ASSERT_EQ(1u, retained_exit.captured_values().size());
        EXPECT_NE(parameter.id(),
                  retained_exit.captured_values()[0].instruction_id());
    }

    TEST(JitRegisterAllocator, PreparesRepresentativeOneBlockProblem)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef lhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef rhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs));
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints = allocator_test_constraints(
            {builder.storage()->instruction(lhs.instruction_id()),
             builder.storage()->instruction(rhs.instruction_id())},
            return_instruction);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_EQ(1u, prepared.block_ranges().size());
        EXPECT_EQ(0u, prepared.block_ranges()[0].range.start.value());
        EXPECT_EQ(8u, prepared.block_ranges()[0].range.end.value());
        ASSERT_EQ(3u, prepared.live_ranges().size());
        ASSERT_EQ(3u, prepared.bundles().size());
        ASSERT_EQ(6u, prepared.occurrences().size());
        ASSERT_EQ(3u, prepared.fixed_constraints().size());

        EXPECT_EQ(1u, prepared.live_ranges()[0].range.start.value());
        EXPECT_EQ(3u, prepared.live_ranges()[0].range.end.value());
        EXPECT_EQ(1u, prepared.live_ranges()[1].range.start.value());
        EXPECT_EQ(3u, prepared.live_ranges()[1].range.end.value());
        EXPECT_EQ(3u, prepared.live_ranges()[2].range.start.value());
        EXPECT_EQ(5u, prepared.live_ranges()[2].range.end.value());
        EXPECT_EQ(3500u, prepared.bundles()[0].spill_weight);
        EXPECT_EQ(3500u, prepared.bundles()[1].spill_weight);
        EXPECT_EQ(3500u, prepared.bundles()[2].spill_weight);

        EXPECT_EQ("allocation {\n"
                  "  bb0 [0, 8) {loop_depth = 0} {\n"
                  "    occurrences {\n"
                  "      1 o0 def l0 result(%0) {fixed = gpr0, weight = 5000}\n"
                  "      1 o1 def l1 result(%1) {fixed = gpr1, weight = 5000}\n"
                  "      2 o2 use l0 operand(%2, 0) {weight = 2000}\n"
                  "      2 o3 use l1 operand(%2, 1) {weight = 2000}\n"
                  "      3 o4 def l2 result(%2) {weight = 4000}\n"
                  "      4 o5 use l2 operand(i3, 0) {fixed = gpr0, weight = "
                  "3000}\n"
                  "    }\n"
                  "\n"
                  "    ranges {\n"
                  "      l0 gpr [1, 3) {origin = %0, occurrences = [o0, o2], "
                  "fixed = [o0:gpr0]}\n"
                  "      l1 gpr [1, 3) {origin = %1, occurrences = [o1, o3], "
                  "fixed = [o1:gpr1]}\n"
                  "      l2 gpr [3, 5) {origin = %2, occurrences = [o4, o5], "
                  "fixed = [o5:gpr0]}\n"
                  "    }\n"
                  "  }\n"
                  "\n"
                  "  bundles {\n"
                  "    b0 gpr [[1, 3):l0] {fixed = [o0:gpr0], priority = 2, "
                  "spill_weight = 3500}\n"
                  "    b1 gpr [[1, 3):l1] {fixed = [o1:gpr1], priority = 2, "
                  "spill_weight = 3500}\n"
                  "    b2 gpr [[3, 5):l2] {fixed = [o5:gpr0], priority = 2, "
                  "spill_weight = 3500}\n"
                  "  }\n"
                  "}\n",
                  format_prepared_allocation(prepared));
    }

    TEST(JitRegisterAllocator,
         KeepsEdgeArgumentsAndBlockParametersInSeparateLocalRanges)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        builder.set_loop_depth(entry, 2);

        TaggedValueRef value =
            emplace_constant(builder, entry, Value::from_smi(7));
        std::array<ProgramValueRef, 1> arguments = {value};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        UnconditionalBranchInstruction branch =
            builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                        edge);
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(exit));
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(exit, parameter);
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            allocator_test_constraints({}, return_instruction, branch);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_EQ(2u, prepared.block_ranges().size());
        EXPECT_EQ(0u, prepared.block_ranges()[0].range.start.value());
        EXPECT_EQ(8u, prepared.block_ranges()[0].range.end.value());
        EXPECT_EQ(8u, prepared.block_ranges()[1].range.start.value());
        EXPECT_EQ(14u, prepared.block_ranges()[1].range.end.value());

        ASSERT_EQ(3u, prepared.live_ranges().size());
        EXPECT_EQ(entry, prepared.live_ranges()[0].block);
        EXPECT_EQ(3u, prepared.live_ranges()[0].range.start.value());
        EXPECT_EQ(7u, prepared.live_ranges()[0].range.end.value());
        EXPECT_EQ(entry, prepared.live_ranges()[1].block);
        EXPECT_EQ(LiveRangeOrigin::Kind::Temporary,
                  prepared.live_ranges()[1].origin.kind());
        EXPECT_EQ(4u, prepared.live_ranges()[1].range.start.value());
        EXPECT_EQ(6u, prepared.live_ranges()[1].range.end.value());
        EXPECT_EQ(exit, prepared.live_ranges()[2].block);
        EXPECT_EQ(9u, prepared.live_ranges()[2].range.start.value());
        EXPECT_EQ(11u, prepared.live_ranges()[2].range.end.value());

        ASSERT_EQ(2u, prepared.live_ranges()[0].occurrences.size());
        OccurrenceId edge_use = prepared.live_ranges()[0].occurrences.back();
        EXPECT_EQ(6u,
                  prepared.occurrences()[edge_use.value()].position.value());
        EXPECT_EQ(OccurrenceAnchor::Kind::BlockEdgeArgument,
                  prepared.occurrences()[edge_use.value()].anchor.kind());
        EXPECT_FALSE(
            prepared.occurrences()[edge_use.value()].register_required);
        EXPECT_EQ(0u, prepared.occurrences()[edge_use.value()].spill_weight);

        OccurrenceId parameter_definition =
            prepared.live_ranges()[2].occurrences.front();
        EXPECT_FALSE(prepared.occurrences()[parameter_definition.value()]
                         .register_required);
        EXPECT_EQ(
            0u,
            prepared.occurrences()[parameter_definition.value()].spill_weight);

        ASSERT_EQ(1u, prepared.bundle_affinities().size());
        const BundleAffinity &affinity = prepared.bundle_affinities().front();
        EXPECT_EQ(BundleAffinityKind::BlockEdge, affinity.kind);
        EXPECT_EQ(edge, affinity.edge);
        EXPECT_EQ(0u, affinity.argument_index);
        EXPECT_EQ(edge_use, affinity.source);
        EXPECT_EQ(parameter_definition, affinity.destination);

        ASSERT_EQ(2u, prepared.bundles().size());
        EXPECT_EQ(2u, prepared.bundles()[0].fragments.size());
        EXPECT_EQ(LiveRangeId(0), prepared.bundles()[0].fragments[0].source);
        EXPECT_EQ(LiveRangeId(2), prepared.bundles()[0].fragments[1].source);
    }

    TEST(JitRegisterAllocator, SchedulesUncoalescedBlockEdgeTransfer)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();

        ConstInstruction value = builder.emplace_instruction<ConstInstruction>(
            entry, Value::from_smi(7));
        std::array<ProgramValueRef, 1> arguments = {TaggedValueRef(value)};
        BlockEdge *edge = builder.make_block_edge(entry, exit, arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                exit, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(value, std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x1)});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x1)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        ASSERT_EQ(2u, prepared.bundles().size());

        auto allocation_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(allocation_result);
        RegisterAllocationResult allocation =
            std::move(allocation_result).value();

        ASSERT_EQ(1u, allocation.transfers().sets().size());
        const BundleTransferSet &set = allocation.transfers().sets().front();
        EXPECT_EQ(TransferPoint::block_edge(edge), set.point);
        ASSERT_EQ(1u, set.transfers.size());
        EXPECT_EQ(x0, allocation.locations()
                          .location_for(set.transfers.front().source)
                          .reg());
        EXPECT_EQ(x1, allocation.locations()
                          .location_for(set.transfers.front().destination)
                          .reg());
    }

    TEST(JitRegisterAllocator, RepeatedBlockEdgeAffinityMergeIsIdempotent)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();

        TaggedValueRef value =
            emplace_constant(builder, entry, Value::from_smi(7));
        std::array<ProgramValueRef, 1> arguments = {value};
        BlockEdge *true_edge = builder.make_block_edge(entry, exit, arguments);
        BlockEdge *false_edge = builder.make_block_edge(entry, exit, arguments);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, value, true_edge, false_edge);
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                exit, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            allocator_test_constraints({}, return_instruction);
        auto prepared_result = prepare_register_allocation(*graph, constraints);

        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        ASSERT_EQ(2u, prepared.bundle_affinities().size());
        ASSERT_EQ(1u, prepared.bundles().size());
        ASSERT_EQ(2u, prepared.bundles().front().fragments.size());
        EXPECT_EQ(LiveRangeId(0),
                  prepared.bundles().front().fragments[0].source);
        EXPECT_EQ(LiveRangeId(1),
                  prepared.bundles().front().fragments[1].source);
    }

    TEST(JitRegisterAllocator, AssignsRepresentativeBundles)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef lhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef rhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs));
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints = allocator_test_constraints(
            {builder.storage()->instruction(lhs.instruction_id()),
             builder.storage()->instruction(rhs.instruction_id())},
            return_instruction);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        const BundleLocationAssignments &assignments = allocation.locations();

        ASSERT_EQ(3u, assignments.size());
        EXPECT_EQ(x0, assignments.location_for(BundleId(0)).reg());
        EXPECT_EQ(x1, assignments.location_for(BundleId(1)).reg());
        EXPECT_EQ(x0, assignments.location_for(BundleId(2)).reg());
        EXPECT_EQ("assignments {\n"
                  "  b0 = gpr0\n"
                  "  b1 = gpr1\n"
                  "  b2 = gpr0\n"
                  "}\n",
                  format_bundle_assignments(assignments));
    }

    TEST(JitRegisterAllocator, FindsOverlapBetweenSortedBundleFragments)
    {
        LiveBundle lhs{
            RegisterClass::GPR,
            {{{LivenessPosition(0), LivenessPosition(2)}, LiveRangeId(0)},
             {{LivenessPosition(4), LivenessPosition(6)}, LiveRangeId(1)},
             {{LivenessPosition(10), LivenessPosition(12)}, LiveRangeId(2)}},
            {},
            0,
            0};
        LiveBundle abutting{
            RegisterClass::GPR,
            {{{LivenessPosition(2), LivenessPosition(4)}, LiveRangeId(3)},
             {{LivenessPosition(6), LivenessPosition(10)}, LiveRangeId(4)},
             {{LivenessPosition(12), LivenessPosition(14)}, LiveRangeId(5)}},
            {},
            0,
            0};
        LiveBundle overlapping{
            RegisterClass::GPR,
            {{{LivenessPosition(2), LivenessPosition(4)}, LiveRangeId(6)},
             {{LivenessPosition(6), LivenessPosition(11)}, LiveRangeId(7)}},
            {},
            0,
            0};

        EXPECT_FALSE(bundles_overlap(lhs, abutting));
        EXPECT_FALSE(bundles_overlap(abutting, lhs));
        EXPECT_TRUE(bundles_overlap(lhs, overlapping));
        EXPECT_TRUE(bundles_overlap(overlapping, lhs));
    }

    TEST(JitRegisterAllocator,
         UsesAllocationOrderAndAllowsAbuttingRangesToShareARegister)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef lhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef rhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs));
        builder.emplace_instruction<BareReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints = gpr_constraints(registers);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        const BundleLocationAssignments &assignments = allocation.locations();

        EXPECT_EQ(x0, assignments.location_for(BundleId(0)).reg());
        EXPECT_EQ(x1, assignments.location_for(BundleId(1)).reg());
        EXPECT_EQ(x0, assignments.location_for(BundleId(2)).reg());
    }

    TEST(JitRegisterAllocator, AssignsLargerBundlesFirst)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef short_lived =
            emplace_constant(builder, entry, Value::from_smi(1));
        TaggedValueRef long_lived =
            emplace_constant(builder, entry, Value::from_smi(2));
        builder.emplace_instruction<AndSMIInstruction>(entry, short_lived,
                                                       short_lived);
        for(size_t index = 0; index < 4; ++index)
        {
            builder.emplace_instruction<UninitializedInstruction>(entry);
        }
        builder.emplace_instruction<BareReturnInstruction>(entry, long_lived);
        ControlFlowGraph *graph = builder.finalize();

        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints = gpr_constraints(registers);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_GT(prepared.bundles()[1].allocation_priority,
                  prepared.bundles()[0].allocation_priority);
        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        const BundleLocationAssignments &assignments = allocation.locations();

        EXPECT_EQ(x1, assignments.location_for(BundleId(0)).reg());
        EXPECT_EQ(x0, assignments.location_for(BundleId(1)).reg());
    }

    TEST(JitRegisterAllocator, EvictsLowerWeightBundleAndRequeuesIt)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(parameter), TaggedValueRef(parameter));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            operation,
            std::vector<ProgramValueUseConstraint>{
                {0, AccessTiming::Late,
                 LocationRequirement::any_register(RegisterClass::GPR)},
                {1, AccessTiming::Late,
                 LocationRequirement::any_register(RegisterClass::GPR)}},
            ResultConstraint{AccessTiming::Late, fixed(x0)});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        ASSERT_GT(prepared.bundles()[0].allocation_priority,
                  prepared.bundles()[1].allocation_priority);
        ASSERT_LT(prepared.bundles()[0].spill_weight,
                  prepared.bundles()[1].spill_weight);

        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        EXPECT_EQ(x1, allocation.locations().location_for(BundleId(0)).reg());
        EXPECT_EQ(x0, allocation.locations().location_for(BundleId(1)).reg());
    }

    TEST(JitRegisterAllocator, FindsARegisterConflictBeforeTheInsertionPoint)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef long_lived =
            emplace_constant(builder, entry, Value::from_smi(1));
        TaggedValueRef later =
            emplace_constant(builder, entry, Value::from_smi(2));
        builder.emplace_instruction<AndSMIInstruction>(entry, later, later);
        for(size_t index = 0; index < 4; ++index)
        {
            builder.emplace_instruction<UninitializedInstruction>(entry);
        }
        builder.emplace_instruction<BareReturnInstruction>(entry, long_lived);
        ControlFlowGraph *graph = builder.finalize();

        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints = gpr_constraints(registers);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_GT(prepared.bundles()[0].allocation_priority,
                  prepared.bundles()[1].allocation_priority);
        ASSERT_GT(prepared.bundles()[1].fragments[0].range.start,
                  prepared.bundles()[0].fragments[0].range.start);
        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        const BundleLocationAssignments &assignments = allocation.locations();

        EXPECT_EQ(x0, assignments.location_for(BundleId(0)).reg());
        EXPECT_EQ(x1, assignments.location_for(BundleId(1)).reg());
    }

    TEST(JitRegisterAllocator, RejectsRegisterPressureWithoutSplitting)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef lhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef rhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs));
        builder.emplace_instruction<BareReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        constexpr std::array registers = {x0};
        AllocationConstraints constraints = gpr_constraints(registers);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        ASSERT_EQ(prepared.bundles()[0].spill_weight,
                  prepared.bundles()[1].spill_weight);

        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result.has_error());
        EXPECT_EQ(RegisterAllocationError::RequiresSplittingOrSpilling,
                  assignment_result.error());
    }

    TEST(JitRegisterAllocator, SplitsBeforeFixedUseUnderRegisterPressure)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(parameter), TaggedValueRef(parameter));
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(
            operation,
            std::vector<ProgramValueUseConstraint>{
                {0, AccessTiming::Late,
                 LocationRequirement::any_register(RegisterClass::GPR)},
                {1, AccessTiming::Late,
                 LocationRequirement::any_register(RegisterClass::GPR)}});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x0)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        ASSERT_EQ(3u, allocation.bundles().size());
        EXPECT_EQ((LivenessRange{LivenessPosition(1), LivenessPosition(4)}),
                  allocation.bundles()[0].fragments[0].range);
        EXPECT_EQ((LivenessRange{LivenessPosition(3), LivenessPosition(4)}),
                  allocation.bundles()[1].fragments[0].range);
        EXPECT_EQ((LivenessRange{LivenessPosition(4), LivenessPosition(5)}),
                  allocation.bundles()[2].fragments[0].range);
        EXPECT_EQ(x0, allocation.locations().location_for(BundleId(0)).reg());
        EXPECT_EQ(x1, allocation.locations().location_for(BundleId(1)).reg());
        EXPECT_EQ(x0, allocation.locations().location_for(BundleId(2)).reg());
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        const BundleTransferSet &set = allocation.transfers().sets().front();
        EXPECT_EQ(TransferPoint::before_instruction(return_instruction),
                  set.point);
        ASSERT_EQ(1u, set.transfers.size());
        EXPECT_EQ(BundleId(1), set.transfers[0].source);
        EXPECT_EQ(BundleId(2), set.transfers[0].destination);
    }

    TEST(JitRegisterAllocator, SplitsAfterFixedUseUnderRegisterPressure)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ConstInstruction fixed_definition =
            builder.emplace_instruction<ConstInstruction>(entry,
                                                          Value::from_smi(1));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(fixed_definition,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        ASSERT_EQ(3u, allocation.bundles().size());
        EXPECT_EQ((LivenessRange{LivenessPosition(1), LivenessPosition(2)}),
                  allocation.bundles()[0].fragments[0].range);
        EXPECT_EQ((LivenessRange{LivenessPosition(3), LivenessPosition(4)}),
                  allocation.bundles()[1].fragments[0].range);
        EXPECT_EQ((LivenessRange{LivenessPosition(2), LivenessPosition(5)}),
                  allocation.bundles()[2].fragments[0].range);
        EXPECT_EQ(x0, allocation.locations().location_for(BundleId(0)).reg());
        EXPECT_EQ(x0, allocation.locations().location_for(BundleId(1)).reg());
        EXPECT_EQ(x1, allocation.locations().location_for(BundleId(2)).reg());
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        const BundleTransferSet &set = allocation.transfers().sets().front();
        EXPECT_EQ(TransferPoint::before_instruction(fixed_definition),
                  set.point);
        ASSERT_EQ(1u, set.transfers.size());
        EXPECT_EQ(BundleId(0), set.transfers[0].source);
        EXPECT_EQ(BundleId(2), set.transfers[0].destination);
    }

    TEST(JitRegisterAllocator, SplitsConflictingFixedRegisters)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x1)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        ASSERT_EQ(2u, allocation.bundles().size());
        EXPECT_EQ((LivenessRange{LivenessPosition(1), LivenessPosition(2)}),
                  allocation.bundles()[0].fragments[0].range);
        EXPECT_EQ((LivenessRange{LivenessPosition(2), LivenessPosition(3)}),
                  allocation.bundles()[1].fragments[0].range);
        EXPECT_EQ(x0, allocation.locations().location_for(BundleId(0)).reg());
        EXPECT_EQ(x1, allocation.locations().location_for(BundleId(1)).reg());
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        const BundleTransferSet &set = allocation.transfers().sets().front();
        EXPECT_EQ(TransferPoint::before_instruction(return_instruction),
                  set.point);
        ASSERT_EQ(1u, set.transfers.size());
        EXPECT_EQ(BundleId(0), set.transfers[0].source);
        EXPECT_EQ(BundleId(1), set.transfers[0].destination);
    }

    TEST(JitRegisterAllocator,
         PreservesFixedStackLocationsForConstraintSplitting)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        LocationRequirement incoming =
            LocationRequirement::fixed(PhysicalLocation::stack(
                StackLocation(StackLocationKind::IncomingParameter, 4)));
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, incoming});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x0)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_EQ(2u, prepared.fixed_constraints().size());
        PhysicalLocation entry_location =
            prepared.fixed_constraints()[0].location;
        ASSERT_TRUE(entry_location.is_stack());
        EXPECT_EQ(StackLocationKind::IncomingParameter,
                  entry_location.stack().kind());
        EXPECT_EQ(4, entry_location.stack().frame_offset());
        EXPECT_EQ(x0, prepared.fixed_constraints()[1].location.reg());

        std::string dump = format_prepared_allocation(prepared);
        EXPECT_NE(std::string::npos,
                  dump.find("fixed = incoming_parameter(4)"));
        EXPECT_NE(std::string::npos, dump.find("fixed = gpr0"));
        EXPECT_NE(std::string::npos,
                  dump.find("fixed = [o0:incoming_parameter(4), o1:gpr0]"));

        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        ASSERT_EQ(2u, allocation.bundles().size());
        PhysicalLocation stack =
            allocation.locations().physical_location_for(BundleId(0));
        ASSERT_TRUE(stack.is_stack());
        EXPECT_EQ(StackLocationKind::IncomingParameter, stack.stack().kind());
        EXPECT_EQ(4, stack.stack().frame_offset());
        EXPECT_EQ(x0, allocation.locations().location_for(BundleId(1)).reg());
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        EXPECT_EQ(TransferPoint::before_instruction(return_instruction),
                  allocation.transfers().sets()[0].point);
    }

    TEST(JitRegisterAllocator,
         LocationIndependentSideExitUseStaysInFixedStackLocation)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);

        ParameterInstruction region_parameter =
            builder.make_instruction<ParameterInstruction>();
        std::array<ProgramValueRef, 1> captured = {
            ProgramValueRef(region_parameter)};
        ExitToInterpreterInstruction exit =
            builder.make_instruction<ExitToInterpreterInstruction>(
                captured, BytecodePCOffset{9});
        std::array<InstructionId, 1> region_parameters = {
            region_parameter.id()};
        std::array<InstructionId, 1> region_instructions = {exit.id()};
        SideExitRegionId region =
            builder
                .make_side_exit_region(region_parameters, region_instructions)
                ->id();
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(parameter)};
        ResumeInInterpreterWithSideExitInstruction owner =
            builder.emplace_instruction<
                ResumeInInterpreterWithSideExitInstruction>(entry, arguments,
                                                            region);
        ControlFlowGraph *graph = builder.finalize();

        LocationRequirement incoming =
            LocationRequirement::fixed(PhysicalLocation::stack(
                StackLocation(StackLocationKind::IncomingParameter, 4)));
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, incoming});
        overrides.emplace_back(
            owner,
            std::vector<ProgramValueUseConstraint>{
                {ResumeInInterpreterWithSideExitInstruction::
                     side_exit_arguments_operand_index,
                 AccessTiming::Late, LocationRequirement::any_location()}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        bool found_side_exit_use = false;
        for(const Occurrence &occurrence: prepared.occurrences())
        {
            if(occurrence.anchor.kind() !=
                   OccurrenceAnchor::Kind::InstructionOperand ||
               occurrence.anchor.instruction_id() != owner.id())
            {
                continue;
            }
            found_side_exit_use = true;
            EXPECT_FALSE(occurrence.register_required);
            EXPECT_EQ(0u, occurrence.spill_weight);
        }
        EXPECT_TRUE(found_side_exit_use);

        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        ASSERT_EQ(1u, allocation.bundles().size());
        PhysicalLocation location =
            allocation.locations().physical_location_for(BundleId(0));
        ASSERT_TRUE(location.is_stack());
        EXPECT_EQ(4, location.stack().frame_offset());
        EXPECT_TRUE(allocation.transfers().sets().empty());
    }

    TEST(JitRegisterAllocator, GroupsSamePointSplitTransfersInParallel)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        LocationRequirement lhs_stack =
            LocationRequirement::fixed(PhysicalLocation::stack(
                StackLocation(StackLocationKind::IncomingParameter, 4)));
        LocationRequirement rhs_stack =
            LocationRequirement::fixed(PhysicalLocation::stack(
                StackLocation(StackLocationKind::IncomingParameter, 3)));
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(lhs, std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, lhs_stack});
        overrides.emplace_back(rhs, std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, rhs_stack});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();

        ASSERT_EQ(1u, allocation.transfers().sets().size());
        const BundleTransferSet &set = allocation.transfers().sets().front();
        EXPECT_EQ(TransferPoint::before_instruction(operation), set.point);
        ASSERT_EQ(2u, set.transfers.size());
        EXPECT_EQ(BundleId(0), set.transfers[0].source);
        EXPECT_EQ(BundleId(3), set.transfers[0].destination);
        EXPECT_EQ(BundleId(1), set.transfers[1].source);
        EXPECT_EQ(BundleId(4), set.transfers[1].destination);

        EXPECT_EQ("allocation_result {\n"
                  "  b0 [[1, 2):l0] = incoming_parameter(4)\n"
                  "  b1 [[1, 2):l1] = incoming_parameter(3)\n"
                  "  b2 [[3, 5):l2] = gpr0\n"
                  "  b3 [[2, 3):l0] = gpr0\n"
                  "  b4 [[2, 3):l1] = gpr1\n"
                  "  transfers {\n"
                  "    before(%2) [b0 -> b3, b1 -> b4]\n"
                  "  }\n"
                  "}\n",
                  format_register_allocation(prepared, allocation));
    }

    TEST(JitRegisterAllocator, RejectsSameInstructionLocationConflict)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        AndSMIInstruction operation =
            builder.emplace_instruction<AndSMIInstruction>(
                entry, TaggedValueRef(parameter), TaggedValueRef(parameter));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(operation));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(operation,
                               std::vector<ProgramValueUseConstraint>{
                                   {0, AccessTiming::Early, fixed(x0)},
                                   {1, AccessTiming::Early, fixed(x1)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result.has_error());
        EXPECT_EQ(RegisterAllocationError::RequiresConstraintFixup,
                  assignment_result.error());
    }

    TEST(JitRegisterAllocator, AvoidsClobberedRegisters)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        std::span<const ProgramValueRef> captured;
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, captured, BytecodePCOffset{7});
        builder.emplace_instruction<BareReturnInstruction>(entry, parameter);
        ControlFlowGraph *graph = builder.finalize();

        RegisterSet clobbers;
        clobbers.insert(x0);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            snapshot, std::vector<ProgramValueUseConstraint>{}, std::nullopt,
            std::vector<TemporaryConstraint>{}, clobbers);
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);

        EXPECT_EQ(x1, assignment_result.value()
                          .locations()
                          .location_for(BundleId(0))
                          .reg());
    }

    TEST(JitRegisterAllocator, SpillsFixedOperandCopyCarrierAcrossTrustedCall)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<TaggedValueRef, 1> arguments = {TaggedValueRef(parameter)};
        TrustedHandlerCallInstruction call =
            builder.emplace_instruction<TrustedHandlerCallInstruction>(
                entry, arguments, allocator_test_trusted_target());
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.push_back(trusted_call_constraints(call));
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x0)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);

        if(!assignment_result)
        {
            FAIL() << "allocation failed with error "
                   << static_cast<int>(assignment_result.error()) << "\n"
                   << format_prepared_allocation(prepared);
        }
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        EXPECT_EQ(1u, allocation.spill_slot_count());
        ASSERT_EQ(4u, allocation.bundles().size());
        EXPECT_EQ((LivenessRange{LivenessPosition(2), LivenessPosition(4)}),
                  allocation.bundles()[2].fragments[0].range);
        ASSERT_TRUE(
            allocation.locations().location_for(BundleId(2)).is_spill_slot());
        EXPECT_EQ(
            SpillSlotId(0),
            allocation.locations().location_for(BundleId(2)).spill_slot());
        ASSERT_EQ(2u, allocation.transfers().sets().size());
        EXPECT_EQ(TransferPoint::before_instruction(call),
                  allocation.transfers().sets()[0].point);
        EXPECT_EQ(TransferPoint::before_instruction(return_instruction),
                  allocation.transfers().sets()[1].point);
    }

    TEST(JitRegisterAllocator, SpillsUnrelatedValueAcrossTrustedCall)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction argument =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction live_through =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<TaggedValueRef, 1> arguments = {TaggedValueRef(argument)};
        TrustedHandlerCallInstruction call =
            builder.emplace_instruction<TrustedHandlerCallInstruction>(
                entry, arguments, allocator_test_trusted_target());
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(live_through));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.push_back(trusted_call_constraints(call));
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x0)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        EXPECT_EQ(1u, allocation.spill_slot_count());
        size_t spill_bundle_count = 0;
        for(size_t index = 0; index < allocation.bundles().size(); ++index)
        {
            BundleLocation location =
                allocation.locations().location_for(BundleId(index));
            if(!location.is_spill_slot())
            {
                continue;
            }
            ++spill_bundle_count;
            ASSERT_EQ(1u, allocation.bundles()[index].fragments.size());
            LiveRangeId source =
                allocation.bundles()[index].fragments[0].source;
            EXPECT_EQ(
                live_through.id(),
                prepared.live_ranges()[source.value()].origin.instruction_id());
        }
        EXPECT_EQ(1u, spill_bundle_count);
    }

    TEST(JitRegisterAllocator, SpillsF64ValueAcrossClobberingBox)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterF64Instruction source =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        ParameterF64Instruction live_through =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            entry, F64Ref(source));
        MovF64Instruction use = builder.emplace_instruction<MovF64Instruction>(
            entry, F64Ref(live_through));
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(box));
        ControlFlowGraph *graph = builder.finalize();

        RegisterSet clobbers;
        clobbers.insert(v0);
        clobbers.insert(v1);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(source, std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(v0)});
        overrides.emplace_back(live_through,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(v1)});
        overrides.emplace_back(
            box,
            std::vector<ProgramValueUseConstraint>{
                {BoxF64Instruction::source_operand_index, AccessTiming::Early,
                 LocationRequirement::fixed_operand_copy(v0)}},
            ResultConstraint{AccessTiming::Late, fixed(x0)},
            std::vector<TemporaryConstraint>{}, clobbers,
            CallLocalSpillPolicy::Allow);
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x0)}});
        constexpr std::array gprs = {x0};
        constexpr std::array simds = {v0, v1};
        std::vector<RegisterClassDefinition> register_classes;
        register_classes.emplace_back(RegisterClass::GPR, gprs);
        register_classes.emplace_back(RegisterClass::SIMD, simds);
        AllocationConstraints constraints(std::move(register_classes),
                                          std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        EXPECT_EQ(1u, allocation.spill_slot_count());
        bool found_live_through_spill = false;
        for(size_t index = 0; index < allocation.bundles().size(); ++index)
        {
            BundleLocation location =
                allocation.locations().location_for(BundleId(index));
            if(!location.is_spill_slot())
            {
                continue;
            }
            ASSERT_EQ(1u, allocation.bundles()[index].fragments.size());
            LiveRangeId spilled_source =
                allocation.bundles()[index].fragments[0].source;
            found_live_through_spill |=
                prepared.live_ranges()[spilled_source.value()]
                    .origin.instruction_id() == live_through.id();
        }
        EXPECT_TRUE(found_live_through_spill);
        EXPECT_EQ(2u, allocation.transfers().sets().size());
        EXPECT_EQ(TransferPoint::before_instruction(box),
                  allocation.transfers().sets()[0].point);
        EXPECT_EQ(TransferPoint::before_instruction(use),
                  allocation.transfers().sets()[1].point);
    }

    TEST(JitRegisterAllocator, RejectsSpillCarrierWithObservableCallOperand)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<TaggedValueRef, 1> arguments = {TaggedValueRef(parameter)};
        TrustedHandlerCallInstruction call =
            builder.emplace_instruction<TrustedHandlerCallInstruction>(
                entry, arguments, allocator_test_trusted_target());
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        RegisterSet clobbers;
        clobbers.insert(x1);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            call,
            std::vector<ProgramValueUseConstraint>{
                {TrustedHandlerCallInstruction::arguments_operand_index,
                 AccessTiming::Early,
                 LocationRequirement::any_register(RegisterClass::GPR)}},
            ResultConstraint{AccessTiming::Late, fixed(x0)},
            std::vector<TemporaryConstraint>{}, clobbers,
            CallLocalSpillPolicy::Allow);
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x0)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result.has_error());
        EXPECT_EQ(RegisterAllocationError::RequiresSplittingOrSpilling,
                  assignment_result.error());
    }

    TEST(JitRegisterAllocator, ReusesCallLocalSpillSlots)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<TaggedValueRef, 1> first_arguments = {TaggedValueRef(first)};
        TrustedHandlerCallInstruction first_call =
            builder.emplace_instruction<TrustedHandlerCallInstruction>(
                entry, first_arguments, allocator_test_trusted_target());
        builder.emplace_instruction<AndSMIInstruction>(
            entry, TaggedValueRef(first), TaggedValueRef(first));
        ConstInstruction second = builder.emplace_instruction<ConstInstruction>(
            entry, Value::from_smi(7));
        std::array<TaggedValueRef, 1> second_arguments = {
            TaggedValueRef(second)};
        TrustedHandlerCallInstruction second_call =
            builder.emplace_instruction<TrustedHandlerCallInstruction>(
                entry, second_arguments, allocator_test_trusted_target());
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(second));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.push_back(trusted_call_constraints(first_call));
        overrides.push_back(trusted_call_constraints(second_call));
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {BareReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x0)}});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        EXPECT_EQ(1u, allocation.spill_slot_count());
        size_t spill_bundle_count = 0;
        for(size_t index = 0; index < allocation.bundles().size(); ++index)
        {
            BundleLocation location =
                allocation.locations().location_for(BundleId(index));
            if(location.is_spill_slot())
            {
                ++spill_bundle_count;
                EXPECT_EQ(SpillSlotId(0), location.spill_slot());
            }
        }
        EXPECT_EQ(2u, spill_bundle_count);
    }

    TEST(JitRegisterAllocator, SplitsBeforeAClobberedFixedRegister)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::span<const ProgramValueRef> captured;
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, captured, BytecodePCOffset{7});
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        RegisterSet clobbers;
        clobbers.insert(x0);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(
            snapshot, std::vector<ProgramValueUseConstraint>{}, std::nullopt,
            std::vector<TemporaryConstraint>{}, clobbers);
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result);
        RegisterAllocationResult allocation =
            std::move(assignment_result).value();
        ASSERT_EQ(2u, allocation.bundles().size());
        EXPECT_EQ((LivenessRange{LivenessPosition(1), LivenessPosition(2)}),
                  allocation.bundles()[0].fragments[0].range);
        EXPECT_EQ((LivenessRange{LivenessPosition(2), LivenessPosition(5)}),
                  allocation.bundles()[1].fragments[0].range);
        EXPECT_EQ(x0, allocation.locations().location_for(BundleId(0)).reg());
        EXPECT_EQ(x1, allocation.locations().location_for(BundleId(1)).reg());
        ASSERT_EQ(1u, allocation.transfers().sets().size());
        const BundleTransferSet &set = allocation.transfers().sets().front();
        EXPECT_EQ(TransferPoint::before_instruction(snapshot), set.point);
        ASSERT_EQ(1u, set.transfers.size());
        EXPECT_EQ(BundleId(0), set.transfers[0].source);
        EXPECT_EQ(BundleId(1), set.transfers[0].destination);
    }

    TEST(JitRegisterAllocator, PreparesTemporaryAndClobberReservations)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(builder.emplace_instruction<AndSMIInstruction>(
            entry, parameter, parameter));
        builder.emplace_instruction<BareReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        RegisterSet clobbers;
        clobbers.insert(x2);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            graph->storage()->instruction(result.instruction_id()),
            std::vector<ProgramValueUseConstraint>{}, std::nullopt,
            std::vector<TemporaryConstraint>{TemporaryConstraint(fixed(x1))},
            clobbers);
        AllocationConstraints constraints(gpr_definition(),
                                          std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_EQ(3u, prepared.live_ranges().size());
        const LiveRange &temporary = prepared.live_ranges()[2];
        EXPECT_EQ(LiveRangeOrigin::Kind::Temporary, temporary.origin.kind());
        EXPECT_EQ(2u, temporary.range.start.value());
        EXPECT_EQ(4u, temporary.range.end.value());
        ASSERT_EQ(1u, temporary.fixed_constraints.size());
        EXPECT_EQ(
            x1,
            prepared.fixed_constraints()[temporary.fixed_constraints[0].value()]
                .location.reg());

        ASSERT_EQ(1u, prepared.clobbers().size());
        EXPECT_EQ(3u, prepared.clobbers()[0].range.start.value());
        EXPECT_EQ(4u, prepared.clobbers()[0].range.end.value());
        EXPECT_EQ(x2, prepared.clobbers()[0].reg);
    }

    TEST(JitRegisterAllocator, GivesDeadDefinitionsMinimalRanges)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        builder.emplace_instruction<UninitializedInstruction>(entry);
        TaggedValueRef result = emplace_constant(builder, entry, Value::None());
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();
        AllocationConstraints constraints =
            allocator_test_constraints({parameter}, return_instruction);

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_EQ(3u, prepared.live_ranges().size());
        EXPECT_EQ(1u, prepared.live_ranges()[0].range.length());
        EXPECT_EQ(1u, prepared.live_ranges()[1].range.length());
        EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
                  prepared.bundles()[0].spill_weight);
        EXPECT_EQ(std::numeric_limits<uint64_t>::max() - 1,
                  prepared.bundles()[1].spill_weight);
    }

    TEST(JitRegisterAllocator,
         KeepsDeadEarlyDefinitionsLiveUntilNextInstruction)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Instruction early_definition =
            builder.emplace_instruction<UninitializedInstruction>(entry);
        TaggedValueRef result = emplace_constant(builder, entry, Value::None());
        builder.emplace_instruction<BareReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            early_definition, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{
                AccessTiming::Early,
                LocationRequirement::any_register(RegisterClass::GPR)});
        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints =
            gpr_constraints(registers, std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_FALSE(prepared.live_ranges().empty());
        const LiveRange &early_range = prepared.live_ranges().front();
        EXPECT_EQ(early_definition.id(), early_range.origin.instruction_id());
        EXPECT_EQ(2u, early_range.range.start.value());
        EXPECT_EQ(4u, early_range.range.end.value());
    }

    TEST(JitRegisterAllocator, GivesSparseLongRangesNonzeroSpillWeight)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        for(size_t index = 0; index < 5000; ++index)
        {
            builder.emplace_instruction<UninitializedInstruction>(entry);
        }
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(entry,
                                                               parameter);
        ControlFlowGraph *graph = builder.finalize();
        AllocationConstraints constraints = allocator_test_constraints(
            {builder.storage()->instruction(parameter.instruction_id())},
            return_instruction);

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_FALSE(prepared.bundles().empty());
        EXPECT_EQ(1u, prepared.bundles().front().spill_weight);
    }

    TEST(JitRegisterAllocator, DerivesRegisterClassesFromRepresentations)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        F64Ref parameter(
            builder.emplace_parameter<ParameterF64Instruction>(entry));
        F64Ref sum(builder.emplace_instruction<AddF64Instruction>(
            entry, parameter, parameter));
        TaggedValueRef boxed(
            builder.emplace_instruction<BoxF64Instruction>(entry, sum));
        builder.emplace_instruction<BareReturnInstruction>(entry, boxed);
        ControlFlowGraph *graph = builder.finalize();

        constexpr std::array gprs = {x0, x1, x2};
        constexpr std::array simds = {v0, v1};
        std::vector<RegisterClassDefinition> register_classes;
        register_classes.emplace_back(RegisterClass::GPR, gprs);
        register_classes.emplace_back(RegisterClass::SIMD, simds);
        AllocationConstraints constraints(std::move(register_classes), {});

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_EQ(3u, prepared.live_ranges().size());
        EXPECT_EQ(RegisterClass::SIMD,
                  prepared.live_ranges()[0].register_class);
        EXPECT_EQ(RegisterClass::SIMD,
                  prepared.live_ranges()[1].register_class);
        EXPECT_EQ(RegisterClass::GPR, prepared.live_ranges()[2].register_class);
    }

    TEST(JitRegisterAllocator, RejectsExecutableSnapshotConsumers)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        std::array<ProgramValueRef, 1> captured = {parameter};
        SnapshotRef snapshot(builder.emplace_instruction<SnapshotInstruction>(
            entry, std::span<const ProgramValueRef>(captured),
            BytecodePCOffset{7}));
        builder.emplace_instruction<ResumeInInterpreterInstruction>(entry,
                                                                    snapshot);
        ControlFlowGraph *graph = builder.finalize();
        AllocationConstraints constraints(gpr_definition(), {});

        auto prepared = prepare_register_allocation(*graph, constraints);

        ASSERT_TRUE(prepared.has_error());
        EXPECT_EQ(RegisterAllocationError::UnsupportedSnapshotConsumer,
                  prepared.error());
    }

    TEST(JitRegisterAllocator, MergesSameAsInputAffinity)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        MovInstruction move =
            builder.emplace_instruction<MovInstruction>(entry, parameter);
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(move));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            move, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             LocationRequirement::same_as_input(0)});
        AllocationConstraints constraints(gpr_definition(),
                                          std::move(overrides));

        auto prepared_result = prepare_register_allocation(*graph, constraints);

        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();
        ASSERT_EQ(1u, prepared.bundle_affinities().size());
        const BundleAffinity &affinity = prepared.bundle_affinities().front();
        EXPECT_EQ(BundleAffinityKind::SameAsInput, affinity.kind);
        EXPECT_EQ(nullptr, affinity.edge);
        EXPECT_EQ(0u, affinity.argument_index);
        ASSERT_EQ(1u, prepared.bundles().size());
        ASSERT_EQ(2u, prepared.bundles().front().fragments.size());
    }

}  // namespace cl::jit
