#include "jit/f64_box_simplification.h"

#include "jit/compilation_session.h"
#include "jit/core_ir_optimization.h"
#include "jit/graph_builder.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <utility>

namespace cl::jit
{
    TEST(JitF64BoxSimplification, FoldsLocalBoxUnboxPair)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterF64Instruction source =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        ParameterF64Instruction rhs =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            entry, F64Ref(source));
        UnboxF64Instruction unbox =
            builder.emplace_instruction<UnboxF64Instruction>(
                entry, TaggedValueRef(box));
        BinaryArithmeticF64Instruction add =
            builder.emplace_instruction<BinaryArithmeticF64Instruction>(
                entry, BinaryArithmeticF64Subkind::AddF64, F64Ref(unbox),
                F64Ref(rhs));
        BoxF64Instruction result =
            builder.emplace_instruction<BoxF64Instruction>(entry, F64Ref(add));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_TRUE(std::move(simplification).value());
        EXPECT_FALSE(box.is_poisoned());
        EXPECT_TRUE(unbox.is_poisoned());
        BinaryArithmeticF64Instruction rewritten_add =
            entry->instruction_at(1).as<BinaryArithmeticF64Instruction>();
        EXPECT_EQ(source.id(), rewritten_add.lhs().instruction_id());
        EXPECT_EQ(rhs.id(), rewritten_add.rhs().instruction_id());
    }

    TEST(JitF64BoxSimplification,
         ConvertsBoxedIncomingJoinAndFoldsTheExposedUnbox)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *first = builder.emplace_block();
        Block *second = builder.emplace_block();
        Block *destination = builder.emplace_block();

        ParameterF64Instruction first_source =
            builder.emplace_parameter<ParameterF64Instruction>(first);
        BoxF64Instruction first_box =
            builder.emplace_instruction<BoxF64Instruction>(
                first, F64Ref(first_source));
        std::array<ProgramValueRef, 1> first_arguments = {
            ProgramValueRef(first_box)};
        BlockEdge *first_edge =
            builder.make_block_edge(first, destination, first_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(first,
                                                                    first_edge);

        ParameterF64Instruction second_source =
            builder.emplace_parameter<ParameterF64Instruction>(second);
        BoxF64Instruction second_box =
            builder.emplace_instruction<BoxF64Instruction>(
                second, F64Ref(second_source));
        std::array<ProgramValueRef, 1> second_arguments = {
            ProgramValueRef(second_box)};
        BlockEdge *second_edge =
            builder.make_block_edge(second, destination, second_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            second, second_edge);

        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(destination);
        UnboxF64Instruction old_unbox =
            builder.emplace_instruction<UnboxF64Instruction>(
                destination, TaggedValueRef(parameter));
        NegF64Instruction old_neg =
            builder.emplace_instruction<NegF64Instruction>(destination,
                                                           F64Ref(old_unbox));
        BoxF64Instruction old_result =
            builder.emplace_instruction<BoxF64Instruction>(destination,
                                                           F64Ref(old_neg));
        builder.emplace_instruction<BareReturnInstruction>(
            destination, TaggedValueRef(old_result));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_TRUE(std::move(simplification).value());
        EXPECT_TRUE(parameter.is_poisoned());
        EXPECT_TRUE(old_unbox.is_poisoned());
        EXPECT_TRUE(old_neg.is_poisoned());
        EXPECT_TRUE(old_result.is_poisoned());
        ASSERT_EQ(1u, destination->parameters().size());
        ParameterF64Instruction replacement =
            destination->parameter_at(0).as<ParameterF64Instruction>();
        ASSERT_EQ(4u, destination->instructions().size());
        BoxF64Instruction destination_box =
            destination->instruction_at(0).as<BoxF64Instruction>();
        NegF64Instruction neg =
            destination->instruction_at(1).as<NegF64Instruction>();
        BoxF64Instruction result =
            destination->instruction_at(2).as<BoxF64Instruction>();
        EXPECT_EQ(replacement.id(), destination_box.source().instruction_id());
        EXPECT_EQ(replacement.id(), neg.source().instruction_id());
        EXPECT_EQ(neg.id(), result.source().instruction_id());
        EXPECT_EQ(result.id(), destination->instruction_at(3)
                                   .as<BareReturnInstruction>()
                                   .return_value()
                                   .instruction_id());
        EXPECT_EQ(
            first_source.id(),
            first->block_successor_edges()[0]->arguments()[0].instruction_id());
        EXPECT_EQ(second_source.id(), second->block_successor_edges()[0]
                                          ->arguments()[0]
                                          .instruction_id());
        EXPECT_FALSE(first_box.is_poisoned());
        EXPECT_FALSE(second_box.is_poisoned());
    }

    TEST(JitF64BoxSimplification, AllowsSnapshotOnlyAliasesOfIncomingBox)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *source = builder.emplace_block();
        Block *destination = builder.emplace_block();
        ParameterF64Instruction floating =
            builder.emplace_parameter<ParameterF64Instruction>(source);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            source, F64Ref(floating));
        std::array<ProgramValueRef, 2> captured = {ProgramValueRef(box),
                                                   ProgramValueRef(box)};
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                source, captured, BytecodePCOffset{17});
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(box)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            source, builder.make_block_edge(source, destination, arguments));
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(destination);
        builder.emplace_instruction<BareReturnInstruction>(
            destination, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_TRUE(std::move(simplification).value());
        EXPECT_TRUE(parameter.is_poisoned());
        EXPECT_FALSE(box.is_poisoned());
        EXPECT_FALSE(snapshot.is_poisoned());
        ASSERT_EQ(2u, snapshot.captured_values().size());
        EXPECT_EQ(box.id(), snapshot.captured_values()[0].instruction_id());
        EXPECT_EQ(box.id(), snapshot.captured_values()[1].instruction_id());
        EXPECT_EQ(floating.id(), source->block_successor_edges()[0]
                                     ->arguments()[0]
                                     .instruction_id());
    }

    TEST(JitF64BoxSimplification, RejectsMixedBoxedAndPreexistingIncomingValues)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *first = builder.emplace_block();
        Block *second = builder.emplace_block();
        Block *destination = builder.emplace_block();
        ParameterF64Instruction floating =
            builder.emplace_parameter<ParameterF64Instruction>(first);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            first, F64Ref(floating));
        std::array<ProgramValueRef, 1> first_arguments = {ProgramValueRef(box)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            first,
            builder.make_block_edge(first, destination, first_arguments));
        ParameterInstruction preexisting =
            builder.emplace_parameter<ParameterInstruction>(second);
        std::array<ProgramValueRef, 1> second_arguments = {
            ProgramValueRef(preexisting)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            second,
            builder.make_block_edge(second, destination, second_arguments));
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(destination);
        builder.emplace_instruction<BareReturnInstruction>(
            destination, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_FALSE(std::move(simplification).value());
        EXPECT_FALSE(parameter.is_poisoned());
        EXPECT_EQ(parameter, destination->parameter_at(0));
    }

    TEST(JitF64BoxSimplification, RejectsIncomingBoxWithOrdinaryInstructionUse)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *source = builder.emplace_block();
        Block *destination = builder.emplace_block();
        ParameterF64Instruction floating =
            builder.emplace_parameter<ParameterF64Instruction>(source);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            source, F64Ref(floating));
        MovInstruction alias = builder.emplace_instruction<MovInstruction>(
            source, TaggedValueRef(box));
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(box)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            source, builder.make_block_edge(source, destination, arguments));
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(destination);
        builder.emplace_instruction<BareReturnInstruction>(
            destination, TaggedValueRef(parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_FALSE(std::move(simplification).value());
        EXPECT_FALSE(parameter.is_poisoned());
        EXPECT_FALSE(alias.is_poisoned());
    }

    TEST(JitF64BoxSimplification, RejectsSameEdgeAliasOfIncomingBox)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *source = builder.emplace_block();
        Block *destination = builder.emplace_block();
        ParameterF64Instruction floating =
            builder.emplace_parameter<ParameterF64Instruction>(source);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            source, F64Ref(floating));
        std::array<ProgramValueRef, 2> arguments = {ProgramValueRef(box),
                                                    ProgramValueRef(box)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            source, builder.make_block_edge(source, destination, arguments));
        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(destination);
        ParameterInstruction second =
            builder.emplace_parameter<ParameterInstruction>(destination);
        builder.emplace_instruction<BareReturnInstruction>(
            destination, TaggedValueRef(first));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_FALSE(std::move(simplification).value());
        EXPECT_FALSE(first.is_poisoned());
        EXPECT_FALSE(second.is_poisoned());
        EXPECT_EQ(first, destination->parameter_at(0));
        EXPECT_EQ(second, destination->parameter_at(1));
    }

    TEST(JitF64BoxSimplification, ConvertsBoxedLoopCarriedRecurrence)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *loop = builder.emplace_block();
        ParameterF64Instruction initial =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction boxed_initial =
            builder.emplace_instruction<BoxF64Instruction>(entry,
                                                           F64Ref(initial));
        std::array<ProgramValueRef, 1> entry_arguments = {
            ProgramValueRef(boxed_initial)};
        BlockEdge *entry_edge =
            builder.make_block_edge(entry, loop, entry_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(entry,
                                                                    entry_edge);

        ParameterInstruction current =
            builder.emplace_parameter<ParameterInstruction>(loop);
        UnboxF64Instruction old_unbox =
            builder.emplace_instruction<UnboxF64Instruction>(
                loop, TaggedValueRef(current));
        NegF64Instruction next = builder.emplace_instruction<NegF64Instruction>(
            loop, F64Ref(old_unbox));
        BoxF64Instruction boxed_next =
            builder.emplace_instruction<BoxF64Instruction>(loop, F64Ref(next));
        std::array<ProgramValueRef, 1> backedge_arguments = {
            ProgramValueRef(boxed_next)};
        BlockEdge *backedge =
            builder.make_block_edge(loop, loop, backedge_arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(loop,
                                                                    backedge);
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_TRUE(std::move(simplification).value());
        EXPECT_TRUE(current.is_poisoned());
        EXPECT_TRUE(old_unbox.is_poisoned());
        ASSERT_EQ(1u, loop->parameters().size());
        ParameterF64Instruction replacement =
            loop->parameter_at(0).as<ParameterF64Instruction>();
        ASSERT_EQ(4u, loop->instructions().size());
        BoxF64Instruction destination_box =
            loop->instruction_at(0).as<BoxF64Instruction>();
        NegF64Instruction rewritten_next =
            loop->instruction_at(1).as<NegF64Instruction>();
        EXPECT_EQ(replacement.id(), destination_box.source().instruction_id());
        EXPECT_EQ(replacement.id(), rewritten_next.source().instruction_id());
        EXPECT_EQ(
            initial.id(),
            entry->block_successor_edges()[0]->arguments()[0].instruction_id());
        EXPECT_EQ(
            rewritten_next.id(),
            loop->block_successor_edges()[0]->arguments()[0].instruction_id());
    }

    TEST(JitF64BoxSimplification, AllowsBoxOnMutuallyExclusiveEdges)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *source = builder.emplace_block();
        Block *first = builder.emplace_block();
        Block *second = builder.emplace_block();
        ParameterInstruction condition =
            builder.emplace_parameter<ParameterInstruction>(source);
        ParameterF64Instruction floating =
            builder.emplace_parameter<ParameterF64Instruction>(source);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            source, F64Ref(floating));
        std::array<ProgramValueRef, 1> first_arguments = {ProgramValueRef(box)};
        std::array<ProgramValueRef, 1> second_arguments = {
            ProgramValueRef(box)};
        BlockEdge *first_edge =
            builder.make_block_edge(source, first, first_arguments);
        BlockEdge *second_edge =
            builder.make_block_edge(source, second, second_arguments);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            source, TaggedValueRef(condition), first_edge, second_edge);
        ParameterInstruction first_parameter =
            builder.emplace_parameter<ParameterInstruction>(first);
        builder.emplace_instruction<BareReturnInstruction>(
            first, TaggedValueRef(first_parameter));
        ParameterInstruction second_parameter =
            builder.emplace_parameter<ParameterInstruction>(second);
        builder.emplace_instruction<BareReturnInstruction>(
            second, TaggedValueRef(second_parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_TRUE(std::move(simplification).value());
        EXPECT_TRUE(first_parameter.is_poisoned());
        EXPECT_TRUE(second_parameter.is_poisoned());
        EXPECT_EQ(
            floating.id(),
            first->predecessor_edges()[0]->arguments()[0].instruction_id());
        EXPECT_EQ(
            floating.id(),
            second->predecessor_edges()[0]->arguments()[0].instruction_id());
        ASSERT_EQ(2u, first->instructions().size());
        ASSERT_EQ(2u, second->instructions().size());
        EXPECT_NE(first->instruction_at(0).id(),
                  second->instruction_at(0).id());
    }

    TEST(JitF64BoxSimplification,
         PreservesDistinctBoxesOfTheSameSourceAcrossSeparateParameters)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *source = builder.emplace_block();
        Block *destination = builder.emplace_block();
        ParameterF64Instruction floating =
            builder.emplace_parameter<ParameterF64Instruction>(source);
        BoxF64Instruction first_box =
            builder.emplace_instruction<BoxF64Instruction>(source,
                                                           F64Ref(floating));
        BoxF64Instruction second_box =
            builder.emplace_instruction<BoxF64Instruction>(source,
                                                           F64Ref(floating));
        std::array<ProgramValueRef, 2> arguments = {
            ProgramValueRef(first_box), ProgramValueRef(second_box)};
        BlockEdge *edge =
            builder.make_block_edge(source, destination, arguments);
        builder.emplace_instruction<UnconditionalBranchInstruction>(source,
                                                                    edge);
        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(destination);
        ParameterInstruction second =
            builder.emplace_parameter<ParameterInstruction>(destination);
        IsNotInstruction distinct =
            builder.emplace_instruction<IsNotInstruction>(
                destination, TaggedValueRef(first), TaggedValueRef(second));
        builder.emplace_instruction<BareReturnInstruction>(
            destination, TaggedValueRef(distinct));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_TRUE(std::move(simplification).value());
        EXPECT_TRUE(first.is_poisoned());
        EXPECT_TRUE(second.is_poisoned());
        ASSERT_EQ(2u, destination->parameters().size());
        EXPECT_EQ(InstructionKind::ParameterF64,
                  destination->parameter_at(0).kind());
        EXPECT_EQ(InstructionKind::ParameterF64,
                  destination->parameter_at(1).kind());
        ASSERT_EQ(4u, destination->instructions().size());
        BoxF64Instruction first_destination_box =
            destination->instruction_at(0).as<BoxF64Instruction>();
        BoxF64Instruction second_destination_box =
            destination->instruction_at(1).as<BoxF64Instruction>();
        EXPECT_NE(first_destination_box.id(), second_destination_box.id());
        IsNotInstruction rewritten_is =
            destination->instruction_at(2).as<IsNotInstruction>();
        EXPECT_EQ(first_destination_box.id(),
                  rewritten_is.lhs().instruction_id());
        EXPECT_EQ(second_destination_box.id(),
                  rewritten_is.rhs().instruction_id());
        EXPECT_EQ(floating.id(), source->block_successor_edges()[0]
                                     ->arguments()[0]
                                     .instruction_id());
        EXPECT_EQ(floating.id(), source->block_successor_edges()[0]
                                     ->arguments()[1]
                                     .instruction_id());
    }

    TEST(JitF64BoxSimplification,
         CorePipelineRemovesGuardAndDeadLocalBoxUnboxPair)
    {
        test::VmTestContext context;
        CompilationSession session{*context.thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterF64Instruction source =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        ParameterF64Instruction rhs =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            entry, F64Ref(source));
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{7});
        PointerAndShapeGuardInstruction guard =
            builder.emplace_instruction<PointerAndShapeGuardInstruction>(
                entry, TaggedValueRef(box), SnapshotRef(snapshot),
                context.vm().float_class()->get_instance_root_shape());
        UnboxF64Instruction unbox =
            builder.emplace_instruction<UnboxF64Instruction>(
                entry, TaggedValueRef(guard));
        BinaryArithmeticF64Instruction add =
            builder.emplace_instruction<BinaryArithmeticF64Instruction>(
                entry, BinaryArithmeticF64Subkind::AddF64, F64Ref(unbox),
                F64Ref(rhs));
        BoxF64Instruction result =
            builder.emplace_instruction<BoxF64Instruction>(entry, F64Ref(add));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        auto optimization = optimize_core_ir(session, *graph);

        ASSERT_TRUE(optimization);
        EXPECT_TRUE(std::move(optimization).value());
        EXPECT_TRUE(box.is_poisoned());
        EXPECT_TRUE(snapshot.is_poisoned());
        EXPECT_TRUE(guard.is_poisoned());
        EXPECT_TRUE(unbox.is_poisoned());
        ASSERT_EQ(3u, entry->instructions().size());
        BinaryArithmeticF64Instruction rewritten_add =
            entry->instruction_at(0).as<BinaryArithmeticF64Instruction>();
        EXPECT_EQ(source.id(), rewritten_add.lhs().instruction_id());
        EXPECT_EQ(rhs.id(), rewritten_add.rhs().instruction_id());
        BoxF64Instruction rewritten_result =
            entry->instruction_at(1).as<BoxF64Instruction>();
        EXPECT_EQ(rewritten_add.id(),
                  rewritten_result.source().instruction_id());
        EXPECT_EQ(rewritten_result.id(), entry->instruction_at(2)
                                             .as<BareReturnInstruction>()
                                             .return_value()
                                             .instruction_id());
    }

    TEST(JitF64BoxSimplification, RetainsBoxWithTaggedUse)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterF64Instruction source =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            entry, F64Ref(source));
        UnboxF64Instruction unbox =
            builder.emplace_instruction<UnboxF64Instruction>(
                entry, TaggedValueRef(box));
        builder.emplace_instruction<BareReturnInstruction>(entry,
                                                           TaggedValueRef(box));
        ControlFlowGraph *graph = builder.finalize();

        auto optimization = optimize_core_ir(session, *graph);

        ASSERT_TRUE(optimization);
        EXPECT_TRUE(std::move(optimization).value());
        EXPECT_FALSE(box.is_poisoned());
        EXPECT_TRUE(unbox.is_poisoned());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(box.id(), entry->instruction_at(0).id());
        EXPECT_EQ(box.id(), entry->instruction_at(1)
                                .as<BareReturnInstruction>()
                                .return_value()
                                .instruction_id());
    }

    TEST(JitF64BoxSimplification, RetainsUnboxOfOtherDefinition)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction source =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterF64Instruction rhs =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        UnboxF64Instruction unbox =
            builder.emplace_instruction<UnboxF64Instruction>(
                entry, TaggedValueRef(source));
        BinaryArithmeticF64Instruction add =
            builder.emplace_instruction<BinaryArithmeticF64Instruction>(
                entry, BinaryArithmeticF64Subkind::AddF64, F64Ref(unbox),
                F64Ref(rhs));
        BoxF64Instruction result =
            builder.emplace_instruction<BoxF64Instruction>(entry, F64Ref(add));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(result));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_f64_boxing(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_FALSE(std::move(simplification).value());
        EXPECT_FALSE(unbox.is_poisoned());
        ASSERT_EQ(4u, entry->instructions().size());
        EXPECT_EQ(unbox.id(), entry->instruction_at(0).id());
    }

}  // namespace cl::jit
