#include "jit/register_allocator.h"

#include "jit/aarch64_allocation_constraints.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"

#include <gtest/gtest.h>

#include <array>
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

        LocationRequirement fixed(PhysicalRegister reg)
        {
            return LocationRequirement::fixed(AllocationLocation::reg(reg));
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
    }  // namespace

    TEST(JitRegisterAllocator, PreparesRepresentativeOneBlockProblem)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef lhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef rhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs));
        builder.emplace_instruction<ReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);
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
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        builder.set_loop_depth(entry, 2);

        TaggedValueRef value =
            emplace_constant(builder, entry, Value::from_smi(7));
        std::array<ProgramValueRef, 1> arguments = {value};
        BlockEdge *edge = builder.make_block_edge(
            entry, exit, std::span<const ProgramValueRef>(arguments));
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    edge);
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(exit));
        builder.emplace_instruction<ReturnInstruction>(exit, parameter);
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);
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
        EXPECT_EQ(6u, prepared.occurrences()[edge_use.value()].point.value());
        EXPECT_EQ(OccurrenceAnchor::Kind::BlockEdgeArgument,
                  prepared.occurrences()[edge_use.value()].anchor.kind());
        EXPECT_GT(prepared.occurrences()[edge_use.value()].spill_weight,
                  10000u);
    }

    TEST(JitRegisterAllocator, AssignsRepresentativeBundles)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef lhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef rhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs));
        builder.emplace_instruction<ReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        BundleRegisterAssignments assignments =
            std::move(assignment_result).value();

        ASSERT_EQ(3u, assignments.size());
        EXPECT_EQ(x0, assignments.register_for(BundleId(0)));
        EXPECT_EQ(x1, assignments.register_for(BundleId(1)));
        EXPECT_EQ(x0, assignments.register_for(BundleId(2)));
        EXPECT_EQ("assignments {\n"
                  "  b0 = gpr0\n"
                  "  b1 = gpr1\n"
                  "  b2 = gpr0\n"
                  "}\n",
                  format_bundle_assignments(assignments));
    }

    TEST(JitRegisterAllocator, FindsOverlapBetweenSortedBundleFragments)
    {
        LiveBundle lhs{RegisterClass::GPR,
                       {{{ProgramPoint(0), ProgramPoint(2)}, LiveRangeId(0)},
                        {{ProgramPoint(4), ProgramPoint(6)}, LiveRangeId(1)},
                        {{ProgramPoint(10), ProgramPoint(12)}, LiveRangeId(2)}},
                       {},
                       0,
                       0};
        LiveBundle abutting{
            RegisterClass::GPR,
            {{{ProgramPoint(2), ProgramPoint(4)}, LiveRangeId(3)},
             {{ProgramPoint(6), ProgramPoint(10)}, LiveRangeId(4)},
             {{ProgramPoint(12), ProgramPoint(14)}, LiveRangeId(5)}},
            {},
            0,
            0};
        LiveBundle overlapping{
            RegisterClass::GPR,
            {{{ProgramPoint(2), ProgramPoint(4)}, LiveRangeId(6)},
             {{ProgramPoint(6), ProgramPoint(11)}, LiveRangeId(7)}},
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
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef lhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef rhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs));
        builder.emplace_instruction<ReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        constexpr std::array registers = {x0, x1};
        AllocationConstraints constraints = gpr_constraints(registers);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        auto assignment_result = assign_bundles(prepared, constraints);
        ASSERT_TRUE(assignment_result);
        BundleRegisterAssignments assignments =
            std::move(assignment_result).value();

        EXPECT_EQ(x0, assignments.register_for(BundleId(0)));
        EXPECT_EQ(x1, assignments.register_for(BundleId(1)));
        EXPECT_EQ(x0, assignments.register_for(BundleId(2)));
    }

    TEST(JitRegisterAllocator, AssignsLargerBundlesFirst)
    {
        CompilationSession session;
        GraphBuilder builder(session);
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
        builder.emplace_instruction<ReturnInstruction>(entry, long_lived);
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
        BundleRegisterAssignments assignments =
            std::move(assignment_result).value();

        EXPECT_EQ(x1, assignments.register_for(BundleId(0)));
        EXPECT_EQ(x0, assignments.register_for(BundleId(1)));
    }

    TEST(JitRegisterAllocator, FindsARegisterConflictBeforeTheInsertionPoint)
    {
        CompilationSession session;
        GraphBuilder builder(session);
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
        builder.emplace_instruction<ReturnInstruction>(entry, long_lived);
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
        BundleRegisterAssignments assignments =
            std::move(assignment_result).value();

        EXPECT_EQ(x0, assignments.register_for(BundleId(0)));
        EXPECT_EQ(x1, assignments.register_for(BundleId(1)));
    }

    TEST(JitRegisterAllocator, RejectsRegisterPressureWithoutSplitting)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef lhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef rhs(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(
            builder.emplace_instruction<AndSMIInstruction>(entry, lhs, rhs));
        builder.emplace_instruction<ReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        constexpr std::array registers = {x0};
        AllocationConstraints constraints = gpr_constraints(registers);
        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        auto assignment_result = assign_bundles(prepared, constraints);

        ASSERT_TRUE(assignment_result.has_error());
        EXPECT_EQ(RegisterAllocationError::RequiresSplittingOrSpilling,
                  assignment_result.error());
    }

    TEST(JitRegisterAllocator, RejectsConflictingFixedRegisters)
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
        overrides.emplace_back(parameter,
                               std::vector<ProgramValueUseConstraint>{},
                               ResultConstraint{AccessTiming::Late, fixed(x0)});
        overrides.emplace_back(
            return_instruction,
            std::vector<ProgramValueUseConstraint>{
                {ReturnInstruction::return_value_operand_index,
                 AccessTiming::Early, fixed(x1)}});
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
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        std::span<const ProgramValueRef> captured;
        SnapshotInstruction *snapshot =
            builder.emplace_instruction<SnapshotInstruction>(entry, captured,
                                                             BytecodePC{7});
        builder.emplace_instruction<ReturnInstruction>(entry, parameter);
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

        EXPECT_EQ(x1, assignment_result.value().register_for(BundleId(0)));
    }

    TEST(JitRegisterAllocator, RejectsAClobberedFixedRegister)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        ParameterInstruction *parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::span<const ProgramValueRef> captured;
        SnapshotInstruction *snapshot =
            builder.emplace_instruction<SnapshotInstruction>(entry, captured,
                                                             BytecodePC{7});
        builder.emplace_instruction<ReturnInstruction>(
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

        ASSERT_TRUE(assignment_result.has_error());
        EXPECT_EQ(RegisterAllocationError::RequiresSplittingOrSpilling,
                  assignment_result.error());
    }

    TEST(JitRegisterAllocator, PreparesTemporaryAndClobberReservations)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        TaggedValueRef result(builder.emplace_instruction<AndSMIInstruction>(
            entry, parameter, parameter));
        builder.emplace_instruction<ReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();

        RegisterSet clobbers;
        clobbers.insert(x2);
        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            result.instruction(), std::vector<ProgramValueUseConstraint>{},
            std::nullopt,
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
                .reg);

        ASSERT_EQ(1u, prepared.clobbers().size());
        EXPECT_EQ(3u, prepared.clobbers()[0].range.start.value());
        EXPECT_EQ(4u, prepared.clobbers()[0].range.end.value());
        EXPECT_EQ(x2, prepared.clobbers()[0].reg);
    }

    TEST(JitRegisterAllocator, GivesDeadDefinitionsMinimalRanges)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        builder.emplace_parameter<ParameterInstruction>(entry);
        builder.emplace_instruction<UninitializedInstruction>(entry);
        TaggedValueRef result = emplace_constant(builder, entry, Value::None());
        builder.emplace_instruction<ReturnInstruction>(entry, result);
        ControlFlowGraph *graph = builder.finalize();
        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);

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

    TEST(JitRegisterAllocator, GivesSparseLongRangesNonzeroSpillWeight)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        for(size_t index = 0; index < 5000; ++index)
        {
            builder.emplace_instruction<UninitializedInstruction>(entry);
        }
        builder.emplace_instruction<ReturnInstruction>(entry, parameter);
        ControlFlowGraph *graph = builder.finalize();
        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);

        auto prepared_result = prepare_register_allocation(*graph, constraints);
        ASSERT_TRUE(prepared_result);
        PreparedAllocationProblem prepared = std::move(prepared_result).value();

        ASSERT_FALSE(prepared.bundles().empty());
        EXPECT_EQ(1u, prepared.bundles().front().spill_weight);
    }

    TEST(JitRegisterAllocator, DerivesRegisterClassesFromRepresentations)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        F64Ref parameter(
            builder.emplace_parameter<ParameterF64Instruction>(entry));
        F64Ref sum(builder.emplace_instruction<AddF64Instruction>(
            entry, parameter, parameter));
        TaggedValueRef boxed(
            builder.emplace_instruction<BoxF64Instruction>(entry, sum));
        builder.emplace_instruction<ReturnInstruction>(entry, boxed);
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
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        std::array<ProgramValueRef, 1> captured = {parameter};
        SnapshotRef snapshot(builder.emplace_instruction<SnapshotInstruction>(
            entry, std::span<const ProgramValueRef>(captured), BytecodePC{7}));
        builder.emplace_instruction<ResumeInInterpreterInstruction>(entry,
                                                                    snapshot);
        builder.emplace_instruction<ReturnInstruction>(entry, parameter);
        ControlFlowGraph *graph = builder.finalize();
        AllocationConstraints constraints =
            make_aarch64_allocation_constraints(*graph);

        auto prepared = prepare_register_allocation(*graph, constraints);

        ASSERT_TRUE(prepared.has_error());
        EXPECT_EQ(RegisterAllocationError::UnsupportedSnapshotConsumer,
                  prepared.error());
    }

    TEST(JitRegisterAllocator, RejectsSameAsInputUntilNormalization)
    {
        CompilationSession session;
        GraphBuilder builder(session);
        Block *entry = builder.emplace_block();
        TaggedValueRef parameter(
            builder.emplace_parameter<ParameterInstruction>(entry));
        MovInstruction *move =
            builder.emplace_instruction<MovInstruction>(entry, parameter);
        builder.emplace_instruction<ReturnInstruction>(entry,
                                                       TaggedValueRef(move));
        ControlFlowGraph *graph = builder.finalize();

        std::vector<InstructionAllocationConstraints> overrides;
        overrides.emplace_back(
            move, std::vector<ProgramValueUseConstraint>{},
            ResultConstraint{AccessTiming::Late,
                             LocationRequirement::same_as_input(0)});
        AllocationConstraints constraints(gpr_definition(),
                                          std::move(overrides));

        auto prepared = prepare_register_allocation(*graph, constraints);

        ASSERT_TRUE(prepared.has_error());
        EXPECT_EQ(RegisterAllocationError::UnsupportedSameAsInput,
                  prepared.error());
    }

}  // namespace cl::jit
