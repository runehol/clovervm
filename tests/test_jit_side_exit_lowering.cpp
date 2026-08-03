#include "jit/side_exit_lowering.h"

#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "object_model/validity_cell.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <utility>

namespace cl::jit
{
    TEST(JitSideExitLowering,
         ClonesSunkInstructionsInProgramOrderAndBindsTheirInputs)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
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
            builder.emplace_instruction<SnapshotInstruction>(
                entry, captured, BytecodePCOffset{17});
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
        EXPECT_TRUE(first_move.is_poisoned());
        EXPECT_TRUE(second_move.is_poisoned());
        EXPECT_TRUE(snapshot.is_poisoned());

        ASSERT_EQ(2u, owner.side_exit_arguments().size());
        EXPECT_EQ(first.id(), owner.side_exit_arguments()[0].instruction_id());
        EXPECT_EQ(second.id(), owner.side_exit_arguments()[1].instruction_id());

        const SideExitRegion &region =
            session.storage()->side_exit_region(owner.side_exit_region());
        ASSERT_EQ(2u, region.parameter_ids().size());
        ASSERT_EQ(3u, region.instruction_ids().size());
        EXPECT_NE(first_move.id(), region.instruction_ids()[0]);
        EXPECT_NE(second_move.id(), region.instruction_ids()[1]);
        EXPECT_NE(snapshot.id(), region.instruction_ids()[2]);
        EXPECT_EQ(InstructionKind::Mov, region.instruction_at(0).kind());
        EXPECT_EQ(InstructionKind::Mov, region.instruction_at(1).kind());
        EXPECT_EQ(InstructionKind::ExitToInterpreter,
                  region.instruction_at(2).kind());
    }

    TEST(JitSideExitLowering, RejectsASelectedSnapshotWithAnExecutableUse)
    {
        EXPECT_DEATH(
            ([] {
                CompilationSession session;
                GraphBuilder builder(session, IRLevel::Core);
                Block *entry = builder.emplace_block();
                ParameterInstruction parameter =
                    builder.emplace_parameter<ParameterInstruction>(entry);
                std::array<ProgramValueRef, 1> captured = {
                    ProgramValueRef(parameter)};
                SnapshotInstruction snapshot =
                    builder.emplace_instruction<SnapshotInstruction>(
                        entry, captured, BytecodePCOffset{23});
                builder.emplace_instruction<CheckNotImplementedInstruction>(
                    entry, TaggedValueRef(parameter), SnapshotRef(snapshot));
                builder.emplace_instruction<BareReturnInstruction>(
                    entry, TaggedValueRef(parameter));
                ControlFlowGraph *graph = builder.finalize();

                SunkInstructionIds sunk;
                sunk.insert(snapshot.id());
                (void)lower_side_exits(session, *graph, sunk);
            }()),
            "not retained by a side exit");
    }

    TEST(JitSideExitLowering, ReplacesInlineTagGuardAndRewritesItsResultUses)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(parameter)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, captured, BytecodePCOffset{31});
        InlineTagGuardInstruction guard =
            builder.emplace_instruction<InlineTagGuardInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot),
                TaggedValueClass::smi_or_boolean());
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(guard));
        ControlFlowGraph *graph = builder.finalize();

        SunkInstructionIds sunk = sink_snapshots(*graph);
        auto lowered = lower_side_exits(session, *graph, sunk);

        ASSERT_TRUE(lowered);
        EXPECT_TRUE(std::move(lowered).value());
        EXPECT_EQ(IRLevel::Machine, graph->ir_level());
        ASSERT_EQ(2u, entry->instructions().size());
        auto owner = entry->instruction_at(0)
                         .as<InlineTagGuardWithSideExitInstruction>();
        EXPECT_TRUE(guard.is_poisoned());
        EXPECT_TRUE(snapshot.is_poisoned());
        EXPECT_EQ(parameter.id(), owner.value().instruction_id());
        EXPECT_EQ(TaggedValueClass::smi_or_boolean(), owner.expected_class());
        ASSERT_EQ(1u, owner.side_exit_arguments().size());
        EXPECT_EQ(parameter.id(),
                  owner.side_exit_arguments()[0].instruction_id());

        BareReturnInstruction rewritten_return =
            entry->instruction_at(1).as<BareReturnInstruction>();
        EXPECT_TRUE(return_instruction.is_poisoned());
        EXPECT_EQ(owner.id(), rewritten_return.return_value().instruction_id());

        const SideExitRegion &region =
            session.storage()->side_exit_region(owner.side_exit_region());
        ASSERT_EQ(1u, region.parameter_ids().size());
        ASSERT_EQ(1u, region.instruction_ids().size());
        EXPECT_NE(snapshot.id(), region.instruction_ids()[0]);
        EXPECT_EQ(InstructionKind::ExitToInterpreter,
                  region.instruction_at(0).kind());
    }

    TEST(JitSideExitLowering, ReplacesShapeAndValidityGuards)
    {
        test::VmTestContext vm;
        ThreadState::ActivationScope activation_scope(vm.thread());
        Shape *shape = vm.vm().str_instance_root_shape();
        ValidityCell *validity = vm.thread()->make_internal_raw<ValidityCell>();

        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(parameter)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, captured, BytecodePCOffset{37});
        ShapeGuardInstruction pointer_and_shape_guard =
            builder.emplace_instruction<PointerAndShapeGuardInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot), shape);
        ShapeGuardInstruction shape_only_guard =
            builder.emplace_instruction<ShapeOnlyGuardInstruction>(
                entry, TaggedValueRef(pointer_and_shape_guard),
                SnapshotRef(snapshot), shape);
        ValidityCellGuardInstruction validity_guard =
            builder.emplace_instruction<ValidityCellGuardInstruction>(
                entry, TaggedValueRef(shape_only_guard), SnapshotRef(snapshot),
                validity);
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(validity_guard));
        ControlFlowGraph *graph = builder.finalize();

        SunkInstructionIds sunk = sink_snapshots(*graph);
        auto lowered = lower_side_exits(session, *graph, sunk);

        ASSERT_TRUE(lowered);
        EXPECT_TRUE(std::move(lowered).value());
        ASSERT_EQ(4u, entry->instructions().size());
        ShapeGuardWithSideExitInstruction lowered_pointer_and_shape =
            entry->instruction_at(0).as<ShapeGuardWithSideExitInstruction>();
        ShapeGuardWithSideExitInstruction lowered_shape_only =
            entry->instruction_at(1).as<ShapeGuardWithSideExitInstruction>();
        ValidityCellGuardWithSideExitInstruction lowered_validity =
            entry->instruction_at(2)
                .as<ValidityCellGuardWithSideExitInstruction>();
        EXPECT_EQ(shape, lowered_pointer_and_shape.expected_shape());
        EXPECT_EQ(
            ShapeGuardWithSideExitSubkind::PointerAndShapeGuardWithSideExit,
            lowered_pointer_and_shape.subkind());
        EXPECT_EQ(ShapeGuardWithSideExitSubkind::ShapeOnlyGuardWithSideExit,
                  lowered_shape_only.subkind());
        EXPECT_EQ(lowered_pointer_and_shape.id(),
                  lowered_shape_only.object().instruction_id());
        EXPECT_EQ(validity, lowered_validity.validity());
        EXPECT_EQ(lowered_shape_only.id(),
                  lowered_validity.value().instruction_id());
        EXPECT_EQ(lowered_pointer_and_shape.side_exit_region(),
                  lowered_shape_only.side_exit_region());
        EXPECT_EQ(lowered_shape_only.side_exit_region(),
                  lowered_validity.side_exit_region());
        EXPECT_EQ(lowered_validity.id(), entry->instruction_at(3)
                                             .as<BareReturnInstruction>()
                                             .return_value()
                                             .instruction_id());
        EXPECT_TRUE(pointer_and_shape_guard.is_poisoned());
        EXPECT_TRUE(shape_only_guard.is_poisoned());
        EXPECT_TRUE(validity_guard.is_poisoned());
        EXPECT_TRUE(return_instruction.is_poisoned());
    }

    TEST(JitSideExitLowering, ReplacesAddSMIAndRewritesItsResultUses)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(lhs)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, captured, BytecodePCOffset{37});
        AddSMIInstruction add = builder.emplace_instruction<AddSMIInstruction>(
            entry, TaggedValueRef(lhs), TaggedValueRef(rhs),
            SnapshotRef(snapshot));
        BareReturnInstruction return_instruction =
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(add));
        ControlFlowGraph *graph = builder.finalize();

        SunkInstructionIds sunk = sink_snapshots(*graph);
        auto lowered = lower_side_exits(session, *graph, sunk);

        ASSERT_TRUE(lowered);
        EXPECT_TRUE(std::move(lowered).value());
        EXPECT_EQ(IRLevel::Machine, graph->ir_level());
        ASSERT_EQ(2u, entry->instructions().size());
        auto owner =
            entry->instruction_at(0).as<AddSMIWithSideExitInstruction>();
        EXPECT_TRUE(add.is_poisoned());
        EXPECT_TRUE(snapshot.is_poisoned());
        EXPECT_EQ(lhs.id(), owner.lhs().instruction_id());
        EXPECT_EQ(rhs.id(), owner.rhs().instruction_id());
        ASSERT_EQ(1u, owner.side_exit_arguments().size());
        EXPECT_EQ(lhs.id(), owner.side_exit_arguments()[0].instruction_id());

        BareReturnInstruction rewritten_return =
            entry->instruction_at(1).as<BareReturnInstruction>();
        EXPECT_TRUE(return_instruction.is_poisoned());
        EXPECT_EQ(owner.id(), rewritten_return.return_value().instruction_id());

        const SideExitRegion &region =
            session.storage()->side_exit_region(owner.side_exit_region());
        ASSERT_EQ(1u, region.parameter_ids().size());
        ASSERT_EQ(1u, region.instruction_ids().size());
        EXPECT_NE(snapshot.id(), region.instruction_ids()[0]);
        EXPECT_EQ(InstructionKind::ExitToInterpreter,
                  region.instruction_at(0).kind());
    }

    TEST(JitSideExitLowering, PreservesBinaryArithmeticSubkinds)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<BinaryArithmeticSMIWithSnapshotSubkind, 3> core_subkinds = {
            BinaryArithmeticSMIWithSnapshotSubkind::AddSMI,
            BinaryArithmeticSMIWithSnapshotSubkind::SubSMI,
            BinaryArithmeticSMIWithSnapshotSubkind::MulSMI};
        std::array<BinaryArithmeticSMIWithSideExitSubkind, 3> machine_subkinds =
            {BinaryArithmeticSMIWithSideExitSubkind::AddSMIWithSideExit,
             BinaryArithmeticSMIWithSideExitSubkind::SubSMIWithSideExit,
             BinaryArithmeticSMIWithSideExitSubkind::MulSMIWithSideExit};
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(lhs)};
        Instruction result = lhs;
        for(size_t index = 0; index < core_subkinds.size(); ++index)
        {
            SnapshotInstruction snapshot =
                builder.emplace_instruction<SnapshotInstruction>(
                    entry, captured,
                    BytecodePCOffset{static_cast<uint32_t>(10 + index)});
            result = builder.emplace_instruction<
                BinaryArithmeticSMIWithSnapshotInstruction>(
                entry, core_subkinds[index], TaggedValueRef(lhs),
                TaggedValueRef(rhs), SnapshotRef(snapshot));
        }
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        SunkInstructionIds sunk = sink_snapshots(*graph);
        auto lowered = lower_side_exits(session, *graph, sunk);

        ASSERT_TRUE(lowered);
        EXPECT_TRUE(std::move(lowered).value());
        ASSERT_EQ(4u, entry->instructions().size());
        for(size_t index = 0; index < machine_subkinds.size(); ++index)
        {
            EXPECT_EQ(machine_subkinds[index],
                      entry->instruction_at(index)
                          .as<BinaryArithmeticSMIWithSideExitInstruction>()
                          .subkind());
        }
    }

    TEST(JitSideExitLowering, RegionResumeCapturesReplacementAddSMIResult)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<ProgramValueRef, 1> add_captured = {ProgramValueRef(lhs)};
        SnapshotInstruction add_snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, add_captured, BytecodePCOffset{11});
        AddSMIInstruction add = builder.emplace_instruction<AddSMIInstruction>(
            entry, TaggedValueRef(lhs), TaggedValueRef(rhs),
            SnapshotRef(add_snapshot));
        std::array<ProgramValueRef, 1> resume_captured = {ProgramValueRef(add)};
        SnapshotInstruction resume_snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, resume_captured, BytecodePCOffset{13});
        builder.emplace_instruction<ResumeInInterpreterInstruction>(
            entry, SnapshotRef(resume_snapshot));
        ControlFlowGraph *graph = builder.finalize();

        SunkInstructionIds sunk = sink_snapshots(*graph);
        auto lowered = lower_side_exits(session, *graph, sunk);

        ASSERT_TRUE(lowered);
        EXPECT_TRUE(std::move(lowered).value());
        ASSERT_EQ(2u, entry->instructions().size());
        auto lowered_add =
            entry->instruction_at(0).as<AddSMIWithSideExitInstruction>();
        auto resume = entry->instruction_at(1)
                          .as<ResumeInInterpreterWithSideExitInstruction>();
        EXPECT_TRUE(add.is_poisoned());
        ASSERT_EQ(1u, resume.side_exit_arguments().size());
        EXPECT_EQ(lowered_add.id(),
                  resume.side_exit_arguments()[0].instruction_id());

        const SideExitRegion &region =
            session.storage()->side_exit_region(resume.side_exit_region());
        ASSERT_EQ(1u, region.parameter_ids().size());
        ASSERT_EQ(1u, region.instruction_ids().size());
        ExitToInterpreterInstruction exit =
            region.instruction_at(0).as<ExitToInterpreterInstruction>();
        ASSERT_EQ(1u, exit.captured_values().size());
        EXPECT_EQ(region.parameter_ids()[0],
                  exit.captured_values()[0].instruction_id());
    }

}  // namespace cl::jit
