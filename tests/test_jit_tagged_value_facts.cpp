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

    TEST(JitTaggedValueSet, CombinesExactShapeFacts)
    {
        test::VmTestContext context;
        Shape *float_shape =
            context.vm().float_class()->get_instance_root_shape();
        Shape *string_shape = context.vm().str_instance_root_shape();
        TaggedValueSet exact_float = TaggedValueSet::exact_shape(float_shape);
        TaggedValueSet exact_string = TaggedValueSet::exact_shape(string_shape);

        ASSERT_TRUE(exact_float.exact_shape().has_value());
        EXPECT_EQ(float_shape, *exact_float.exact_shape());
        EXPECT_EQ(exact_float, TaggedValueSet::never().merge(exact_float));
        EXPECT_EQ(exact_float, exact_float.merge(exact_float));
        EXPECT_EQ(TaggedValueSet::pointer(),
                  exact_float.merge(TaggedValueSet::pointer()));
        EXPECT_EQ(exact_float,
                  exact_float.intersect(TaggedValueSet::pointer()));
        EXPECT_TRUE(exact_float.intersect(exact_string).is_never());
        EXPECT_TRUE(exact_float.is_subset_of(TaggedValueSet::pointer()));
        EXPECT_TRUE(exact_float.is_disjoint_from(exact_string));

        TaggedValueSet refcounted_float = exact_float.intersect(
            TaggedValueSet::from_inline_tag(value_refcounted_ptr_tag));
        TaggedValueSet interned_float = exact_float.intersect(
            TaggedValueSet::from_inline_tag(value_interned_ptr_tag));
        EXPECT_EQ(tagged_value_set_bit(value_refcounted_ptr_tag),
                  refcounted_float.bits());
        EXPECT_EQ(tagged_value_set_bit(value_interned_ptr_tag),
                  interned_float.bits());
        ASSERT_TRUE(refcounted_float.exact_shape().has_value());
        ASSERT_TRUE(interned_float.exact_shape().has_value());
        EXPECT_EQ(float_shape, *refcounted_float.exact_shape());
        EXPECT_EQ(float_shape, *interned_float.exact_shape());
        EXPECT_TRUE(refcounted_float.is_disjoint_from(interned_float));
        EXPECT_EQ(exact_float, refcounted_float.merge(interned_float));

        TaggedValueSet float_or_smi = exact_float.merge(TaggedValueSet::smi());
        // The current domain cannot constrain only the pointer arm of a mixed
        // set, so this deliberately widens to SMI or any pointer. A future
        // pointer-arm shape-set extension could retain exact Float here.
        EXPECT_EQ(exact_float.bits() | TaggedValueSet::smi().bits(),
                  float_or_smi.bits());
        EXPECT_FALSE(float_or_smi.exact_shape().has_value());
    }

    TEST(JitTaggedValueFactAnalysis, ConvergesLoopParameterFacts)
    {
        CompilationSession session{test::compiler_thread()};
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

    TEST(JitTaggedValueFactAnalysis, RetainsExactFloatShapeAcrossMerge)
    {
        test::VmTestContext context;
        CompilationSession session{*context.thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *join = builder.emplace_block();
        ParameterF64Instruction lhs =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        ParameterF64Instruction rhs =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction boxed_lhs =
            builder.emplace_instruction<BoxF64Instruction>(entry, F64Ref(lhs));
        BoxF64Instruction boxed_rhs =
            builder.emplace_instruction<BoxF64Instruction>(entry, F64Ref(rhs));
        ConstInstruction condition =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        std::array<ProgramValueRef, 1> lhs_arguments = {
            ProgramValueRef(boxed_lhs)};
        std::array<ProgramValueRef, 1> rhs_arguments = {
            ProgramValueRef(boxed_rhs)};
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(condition),
            builder.make_block_edge(entry, join, lhs_arguments),
            builder.make_block_edge(entry, join, rhs_arguments));
        ParameterInstruction merged =
            builder.emplace_parameter<ParameterInstruction>(join);
        builder.emplace_instruction<BareReturnInstruction>(
            join, TaggedValueRef(merged));
        ControlFlowGraph *graph = builder.finalize();

        GraphQueries queries =
            graph->prepare_queries(GraphQuery::TaggedValueFacts);
        const TaggedValueSet &facts =
            queries.tagged_value_facts_of(ProgramValueRef(merged));
        ASSERT_TRUE(facts.exact_shape().has_value());
        EXPECT_EQ(context.thread()
                      ->class_for_native_layout(NativeLayoutId::Float)
                      ->get_instance_root_shape(),
                  *facts.exact_shape());
    }

    TEST(JitTaggedValueFactAnalysis,
         ShapeGuardsRetainExactFactsOnlyForImmutableShapes)
    {
        test::VmTestContext context;
        CompilationSession session{*context.thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{7});
        Shape *immutable_shape = context.vm().str_instance_root_shape();
        Shape *mutable_shape =
            context.vm().list_class()->get_instance_root_shape();
        PointerAndShapeGuardInstruction immutable_guard =
            builder.emplace_instruction<PointerAndShapeGuardInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot),
                immutable_shape);
        PointerAndShapeGuardInstruction mutable_guard =
            builder.emplace_instruction<PointerAndShapeGuardInstruction>(
                entry, TaggedValueRef(parameter), SnapshotRef(snapshot),
                mutable_shape);
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(mutable_guard));
        ControlFlowGraph *graph = builder.finalize();

        GraphQueries queries =
            graph->prepare_queries(GraphQuery::TaggedValueFacts);
        const TaggedValueSet &immutable_facts =
            queries.tagged_value_facts_of(ProgramValueRef(immutable_guard));
        ASSERT_TRUE(immutable_facts.exact_shape().has_value());
        EXPECT_EQ(immutable_shape, *immutable_facts.exact_shape());

        const TaggedValueSet &mutable_facts =
            queries.tagged_value_facts_of(ProgramValueRef(mutable_guard));
        EXPECT_EQ(TaggedValueSet::pointer(), mutable_facts);
        EXPECT_FALSE(mutable_facts.exact_shape().has_value());
    }

    TEST(JitTaggedValueGuardSimplification,
         RemovesGuardProvedByComparisonAndDeadSnapshot)
    {
        CompilationSession session{test::compiler_thread()};
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
        CompilationSession session{test::compiler_thread()};
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

        CompilationSession session{test::compiler_thread()};
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

    TEST(JitTaggedValueGuardSimplification,
         RemovesFloatShapeGuardProvedByBoxF64)
    {
        test::VmTestContext context;
        CompilationSession session{*context.thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterF64Instruction source =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            entry, F64Ref(source));
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{7});
        PointerAndShapeGuardInstruction guard =
            builder.emplace_instruction<PointerAndShapeGuardInstruction>(
                entry, TaggedValueRef(box), SnapshotRef(snapshot),
                context.thread()
                    ->class_for_native_layout(NativeLayoutId::Float)
                    ->get_instance_root_shape());
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(guard));
        ControlFlowGraph *graph = builder.finalize();

        auto optimization = optimize_core_ir(session, *graph);

        ASSERT_TRUE(optimization);
        EXPECT_TRUE(std::move(optimization).value());
        EXPECT_TRUE(guard.is_poisoned());
        EXPECT_TRUE(snapshot.is_poisoned());
        ASSERT_EQ(2u, entry->instructions().size());
        EXPECT_EQ(box.id(), entry->instruction_at(0).id());
        EXPECT_EQ(box.id(), entry->instruction_at(1)
                                .as<BareReturnInstruction>()
                                .return_value()
                                .instruction_id());
    }

    TEST(JitTaggedValueGuardSimplification,
         RetainsMismatchedShapeGuardAfterBoxF64)
    {
        test::VmTestContext context;
        CompilationSession session{*context.thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        ParameterF64Instruction source =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            entry, F64Ref(source));
        SnapshotInstruction snapshot =
            builder.emplace_instruction<SnapshotInstruction>(
                entry, std::span<const ProgramValueRef>{}, BytecodePCOffset{7});
        PointerAndShapeGuardInstruction guard =
            builder.emplace_instruction<PointerAndShapeGuardInstruction>(
                entry, TaggedValueRef(box), SnapshotRef(snapshot),
                context.vm().str_instance_root_shape());
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(guard));
        ControlFlowGraph *graph = builder.finalize();

        auto simplification = simplify_tagged_value_guards(session, *graph);

        ASSERT_TRUE(simplification);
        EXPECT_TRUE(std::move(simplification).value());
        EXPECT_TRUE(guard.is_poisoned());
        ASSERT_EQ(4u, entry->instructions().size());
        ShapeGuardInstruction retained =
            entry->instruction_at(2).as<ShapeGuardInstruction>();
        EXPECT_EQ(ShapeGuardSubkind::ShapeOnlyGuard, retained.subkind());
        EXPECT_EQ(context.vm().str_instance_root_shape(),
                  retained.expected_shape());
    }

}  // namespace cl::jit
