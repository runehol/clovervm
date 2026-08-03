#include "jit/compilation_session.h"
#include "jit/core_ir_optimization.h"
#include "jit/graph_builder.h"
#include "jit/graph_queries.h"
#include "jit/tagged_value_facts.h"
#include "jit/tagged_value_guard_simplification.h"
#include "object_model/class_object.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <utility>

namespace cl::jit
{
    TEST(JitTaggedValueClass, EncodesNamedClasses)
    {
        TaggedValueClass boolean = TaggedValueClass::boolean();
        EXPECT_EQ(TaggedValueClassKind::MaskedEqual, boolean.kind());
        EXPECT_EQ(value_tag_mask, boolean.mask());
        EXPECT_EQ(value_boolean_tag, boolean.expected());

        TaggedValueClass pointer = TaggedValueClass::pointer();
        EXPECT_EQ(TaggedValueClassKind::MaskedNonZero, pointer.kind());
        EXPECT_EQ(value_ptr_mask, pointer.mask());
        EXPECT_EQ(0u, pointer.expected());
        EXPECT_EQ(boolean, TaggedValueClass::from_encoded(boolean.encoded()));
        EXPECT_EQ(pointer, TaggedValueClass::from_encoded(pointer.encoded()));
    }

    TEST(JitTaggedValueSet, ConstructsAndCombinesFacts)
    {
        TaggedValueSet smi = TaggedValueSet::smi();
        TaggedValueSet boolean = TaggedValueSet::boolean();
        TaggedValueSet smi_or_boolean = TaggedValueSet::smi_or_boolean();

        EXPECT_TRUE(TaggedValueSet::never().is_never());
        EXPECT_EQ(valid_tagged_value_set_bits,
                  TaggedValueSet::unknown().bits());
        EXPECT_TRUE(smi.is_subset_of(smi_or_boolean));
        EXPECT_TRUE(boolean.is_subset_of(smi_or_boolean));
        EXPECT_TRUE(smi.is_disjoint_from(boolean));
        EXPECT_EQ(smi_or_boolean, smi.merge(boolean));
        EXPECT_TRUE(smi.intersect(boolean).is_never());

        EXPECT_EQ(smi, TaggedValueSet::from_class(TaggedValueClass::smi()));
        EXPECT_EQ(boolean,
                  TaggedValueSet::from_class(TaggedValueClass::boolean()));
        EXPECT_EQ(smi_or_boolean, TaggedValueSet::from_class(
                                      TaggedValueClass::smi_or_boolean()));

        TaggedValueSet inline_values =
            TaggedValueSet::from_class(TaggedValueClass::any_inline());
        TaggedValueSet pointers =
            TaggedValueSet::from_class(TaggedValueClass::pointer());
        EXPECT_EQ(TaggedValueSet::unknown(), inline_values.merge(pointers));
        EXPECT_TRUE(inline_values.is_disjoint_from(pointers));

        TaggedValueClass broad_boolean = TaggedValueClass::masked_equal(
            uint8_t(value_special_mask), uint8_t(value_boolean_tag));
        EXPECT_EQ(boolean, TaggedValueSet::from_class(broad_boolean));

        TaggedValueClass invalid_exact = TaggedValueClass::masked_equal(
            uint8_t(value_tag_mask), uint8_t(0x07));
        EXPECT_TRUE(TaggedValueSet::from_class(invalid_exact).is_never());
    }

    TEST(JitTaggedValueSet, ConstructsFromShapeKeys)
    {
        EXPECT_EQ(TaggedValueSet::boolean(),
                  TaggedValueSet::from_shape_key(
                      ShapeKey::from_value(Value::True())));

        test::VmTestContext context;
        ShapeKey float_shape = ShapeKey::from_shape(
            context.vm().float_class()->get_instance_root_shape());
        EXPECT_EQ(TaggedValueSet::pointer(),
                  TaggedValueSet::from_shape_key(float_shape));
    }

    TEST(JitTaggedValueFactAnalysis, ConvergesLoopParameterFacts)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *loop = builder.emplace_block();

        ConstInstruction smi = builder.emplace_instruction<ConstInstruction>(
            entry, Value::from_smi(1));
        std::array<ProgramValueRef, 1> entry_arguments = {ProgramValueRef(smi)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            entry, builder.make_block_edge(entry, loop, entry_arguments));

        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(loop);
        IsInstruction comparison = builder.emplace_instruction<IsInstruction>(
            loop, TaggedValueRef(parameter), TaggedValueRef(parameter));
        std::array<ProgramValueRef, 1> backedge_arguments = {
            ProgramValueRef(comparison)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            loop, builder.make_block_edge(loop, loop, backedge_arguments));
        ControlFlowGraph *graph = builder.finalize();

        GraphQueries queries =
            graph->prepare_queries(GraphQuery::TaggedValueFacts);
        EXPECT_EQ(TaggedValueSet::smi_or_boolean(),
                  queries.tagged_value_facts_of(ProgramValueRef(parameter)));
        EXPECT_EQ(TaggedValueSet::boolean(),
                  queries.tagged_value_facts_of(ProgramValueRef(comparison)));
    }

    TEST(JitTaggedValueGuardSimplification,
         RemovesGuardProvedByComparisonAndDeadSnapshot)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction lhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ParameterInstruction rhs =
            builder.emplace_parameter<ParameterInstruction>(entry);
        IsInstruction comparison = builder.emplace_instruction<IsInstruction>(
            entry, TaggedValueRef(lhs), TaggedValueRef(rhs));
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{7});
        InlineTagGuardInstruction guard =
            builder.emplace_instruction<InlineTagGuardInstruction>(
                entry, TaggedValueRef(comparison), SnapshotRef(snapshot),
                TaggedValueClass::any_inline());
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(guard));
        ControlFlowGraph *graph = builder.finalize();

        auto optimization = optimize_core_ir(session, *graph);

        ASSERT_TRUE(optimization);
        EXPECT_TRUE(std::move(optimization).value());
        EXPECT_TRUE(guard.is_poisoned());
        EXPECT_TRUE(snapshot.is_poisoned());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(comparison.id(), entry->instruction_at(0).id());
        EXPECT_EQ(comparison.id(), entry->instruction_at(1)
                                       .as<BareReturnInstruction>()
                                       .return_value()
                                       .instruction_id());
    }

    TEST(JitTaggedValueGuardSimplification, RetainsGuardForUnknownParameter)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{7});
        InlineTagGuardInstruction guard =
            builder.emplace_instruction<InlineTagGuardInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot),
                TaggedValueClass::smi());
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(guard));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_tagged_value_guards(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_FALSE(std::move(simplification).value());
        EXPECT_FALSE(guard.is_poisoned());
    }

    TEST(JitTaggedValueGuardSimplification,
         WeakensShapeGuardOnlyForProvenPointer)
    {
        test::VmTestContext vm;
        ThreadState::ActivationScope activation_scope(vm.thread());
        Shape *shape = vm.vm().str_instance_root_shape();

        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{7});
        PointerAndShapeGuardInstruction unknown_guard =
            builder.emplace_instruction<PointerAndShapeGuardInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot), shape);
        InlineTagGuardInstruction pointer_guard =
            builder.emplace_instruction<InlineTagGuardInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot),
                TaggedValueClass::pointer());
        PointerAndShapeGuardInstruction proven_guard =
            builder.emplace_instruction<PointerAndShapeGuardInstruction>(
                entry, TaggedValueRef(pointer_guard), SnapshotRef(snapshot),
                shape);
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(proven_guard));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_tagged_value_guards(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_TRUE(std::move(simplification).value());
        EXPECT_FALSE(unknown_guard.is_poisoned());
        EXPECT_TRUE(proven_guard.is_poisoned());
        ASSERT_EQ(5u, entry->instructions().size());
        EXPECT_EQ(InstructionKind::PointerAndShapeGuard,
                  entry->instruction_at(1).kind());
        ShapeGuardInstruction weakened =
            entry->instruction_at(3).as<ShapeGuardInstruction>();
        EXPECT_EQ(ShapeGuardSubkind::ShapeOnlyGuard, weakened.subkind());
        EXPECT_EQ(pointer_guard.id(), weakened.object().instruction_id());
        EXPECT_EQ(weakened.id(), entry->instruction_at(4)
                                     .as<BareReturnInstruction>()
                                     .return_value()
                                     .instruction_id());
    }

}  // namespace cl::jit
