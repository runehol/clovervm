#include "jit/constant_folding.h"

#include "builtin_types/float.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "object_model/owned.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <utility>

namespace cl::jit
{
    TEST(JitConstantFolding, FoldsNormalizedUnboxNegChain)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());
        constexpr uint64_t input_bits = 0x4004000000000000;
        Owned<TValue<Float>> floating(
            context.thread()->make_object_value<Float>(
                std::bit_cast<double>(input_bits)));

        CompilationSession session{*context.thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Value retained = builder.retain_and_pin_value(floating.raw_value());
        ConstInstruction constant =
            builder.emplace_instruction<ConstInstruction>(entry, retained);
        UnboxF64Instruction unbox =
            builder.emplace_instruction<UnboxF64Instruction>(
                entry, TaggedValueRef(constant));
        NegF64Instruction neg = builder.emplace_instruction<NegF64Instruction>(
            entry, F64Ref(unbox));
        BoxF64Instruction boxed =
            builder.emplace_instruction<BoxF64Instruction>(entry, F64Ref(neg));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(boxed));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_TRUE(std::move(folding).value());
        EXPECT_TRUE(unbox.is_poisoned());
        EXPECT_TRUE(neg.is_poisoned());
        ASSERT_EQ(5u, entry->instructions().size());
        ConstF64Instruction result =
            entry->instruction_at(2).as<ConstF64Instruction>();
        double expected = -std::bit_cast<double>(input_bits);
        EXPECT_EQ(std::bit_cast<uint64_t>(expected), result.bits());
        EXPECT_EQ(result.id(), entry->instruction_at(3)
                                   .as<BoxF64Instruction>()
                                   .source()
                                   .instruction_id());
    }

    TEST(JitConstantFolding, UsesHostArithmeticForF64Negation)
    {
        constexpr std::array values = {
            uint64_t{0x0000000000000000}, uint64_t{0x8000000000000000},
            uint64_t{0x7ff0000000000000}, uint64_t{0x7ff8000000001234}};

        for(uint64_t bits: values)
        {
            CompilationSession session{test::compiler_thread()};
            GraphBuilder builder(session, IRLevel::Core);
            Block *entry = builder.emplace_block();
            ConstF64Instruction constant =
                builder.emplace_instruction<ConstF64Instruction>(entry, bits);
            NegF64Instruction neg =
                builder.emplace_instruction<NegF64Instruction>(
                    entry, F64Ref(constant));
            BoxF64Instruction boxed =
                builder.emplace_instruction<BoxF64Instruction>(entry,
                                                               F64Ref(neg));
            builder.emplace_instruction<BareReturnInstruction>(
                entry, TaggedValueRef(boxed));
            ControlFlowGraph *graph = builder.finalize();

            auto folding = fold_constants(session, *graph);

            ASSERT_TRUE(folding);
            EXPECT_TRUE(std::move(folding).value());
            EXPECT_TRUE(neg.is_poisoned());
            ConstF64Instruction result =
                entry->instruction_at(1).as<ConstF64Instruction>();
            double expected = -std::bit_cast<double>(bits);
            EXPECT_EQ(std::bit_cast<uint64_t>(expected), result.bits());
        }
    }

    TEST(JitConstantFolding, DoesNotUnboxFloatSubclassConstant)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());
        Owned<Value> subclass_class(
            context.run_file(L"class FloatSubclass(float):\n"
                             L"    pass\n"
                             L"FloatSubclass\n"));
        ASSERT_TRUE(can_convert_to<ClassObject>(subclass_class.raw_value()));
        Owned<TValue<Float>> subclass(
            context.thread()->make_internal_value<Float>(
                assume_convert_to<ClassObject>(subclass_class.raw_value()),
                1.5));
        ASSERT_NE(context.vm().float_class()->get_instance_root_shape(),
                  subclass.raw_value().get_ptr<Object>()->get_shape());

        CompilationSession session{*context.thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Value retained = builder.retain_and_pin_value(subclass.raw_value());
        ConstInstruction constant =
            builder.emplace_instruction<ConstInstruction>(entry, retained);
        UnboxF64Instruction unbox =
            builder.emplace_instruction<UnboxF64Instruction>(
                entry, TaggedValueRef(constant));
        BoxF64Instruction boxed =
            builder.emplace_instruction<BoxF64Instruction>(entry,
                                                           F64Ref(unbox));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(boxed));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_FALSE(std::move(folding).value());
        EXPECT_FALSE(unbox.is_poisoned());
    }

    TEST(JitConstantFolding, UsesTaggedObjectIdentityForJoins)
    {
        test::VmTestContext context;
        ThreadState::ActivationScope activation_scope(context.thread());
        Owned<TValue<Float>> first(
            context.thread()->make_object_value<Float>(1.5));
        Owned<TValue<Float>> second(
            context.thread()->make_object_value<Float>(1.5));
        ASSERT_NE(first.raw_value(), second.raw_value());

        for(bool same_identity: {false, true})
        {
            CompilationSession session{*context.thread()};
            GraphBuilder builder(session, IRLevel::Core);
            Block *entry = builder.emplace_block();
            Block *exit = builder.emplace_block();
            Value first_value = builder.retain_and_pin_value(first.raw_value());
            Value second_value = builder.retain_and_pin_value(
                same_identity ? first.raw_value() : second.raw_value());
            ConstInstruction first_constant =
                builder.emplace_instruction<ConstInstruction>(entry,
                                                              first_value);
            ConstInstruction second_constant =
                builder.emplace_instruction<ConstInstruction>(entry,
                                                              second_value);
            ConstInstruction condition =
                builder.emplace_instruction<ConstInstruction>(entry,
                                                              Value::True());
            std::array<ProgramValueRef, 1> first_arguments = {
                ProgramValueRef(first_constant)};
            std::array<ProgramValueRef, 1> second_arguments = {
                ProgramValueRef(second_constant)};
            BlockEdge *first_edge =
                builder.make_block_edge(entry, exit, first_arguments);
            BlockEdge *second_edge =
                builder.make_block_edge(entry, exit, second_arguments);
            builder.emplace_instruction<ConditionalBranchInstruction>(
                entry, TaggedValueRef(condition), first_edge, second_edge);
            ParameterInstruction parameter =
                builder.emplace_parameter<ParameterInstruction>(exit);
            builder.emplace_instruction<BareReturnInstruction>(
                exit, TaggedValueRef(parameter));
            ControlFlowGraph *graph = builder.finalize();

            auto folding = fold_constants(session, *graph);

            ASSERT_TRUE(folding);
            EXPECT_EQ(same_identity, std::move(folding).value());
            EXPECT_EQ(same_identity, parameter.is_poisoned());
            if(same_identity)
            {
                EXPECT_EQ(
                    first.raw_value(),
                    exit->instruction_at(0).as<ConstInstruction>().constant());
            }
        }
    }

    TEST(JitConstantFolding, FoldsTaggedConstantAndSelfBackedge)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *loop = builder.emplace_block();

        ConstInstruction constant =
            builder.emplace_instruction<ConstInstruction>(entry,
                                                          Value::from_smi(42));
        std::array<ProgramValueRef, 1> entry_arguments = {
            ProgramValueRef(constant)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            entry, builder.make_block_edge(entry, loop, entry_arguments));

        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(loop);
        std::array<ProgramValueRef, 1> captured = {ProgramValueRef(parameter)};
        builder.emplace_instruction<SnapshotInstruction>(loop, captured,
                                                         BytecodePCOffset{7});
        std::array<ProgramValueRef, 1> backedge_arguments = {
            ProgramValueRef(parameter)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            loop, builder.make_block_edge(loop, loop, backedge_arguments));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_TRUE(std::move(folding).value());
        EXPECT_TRUE(parameter.is_poisoned());
        EXPECT_TRUE(loop->parameters().empty());
        ASSERT_EQ(3u, loop->instructions().size());
        ConstInstruction materialized =
            loop->instruction_at(0).as<ConstInstruction>();
        EXPECT_EQ(Value::from_smi(42), materialized.constant());
        SnapshotInstruction snapshot =
            loop->instruction_at(1).as<SnapshotInstruction>();
        EXPECT_EQ(materialized.id(),
                  snapshot.captured_values()[0].instruction_id());
        EXPECT_TRUE(entry->block_successor_edges()[0]->arguments().empty());
        EXPECT_TRUE(loop->block_successor_edges()[0]->arguments().empty());
    }

    TEST(JitConstantFolding, PropagatesConstantsThroughParameterChains)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *middle = builder.emplace_block();
        Block *exit = builder.emplace_block();

        ConstInstruction constant =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        std::array<ProgramValueRef, 1> entry_arguments = {
            ProgramValueRef(constant)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            entry, builder.make_block_edge(entry, middle, entry_arguments));

        ParameterInstruction middle_parameter =
            builder.emplace_parameter<ParameterInstruction>(middle);
        std::array<ProgramValueRef, 1> middle_arguments = {
            ProgramValueRef(middle_parameter)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            middle, builder.make_block_edge(middle, exit, middle_arguments));

        ParameterInstruction exit_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<BareReturnInstruction>(
            exit, TaggedValueRef(exit_parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_TRUE(std::move(folding).value());
        EXPECT_TRUE(middle_parameter.is_poisoned());
        EXPECT_TRUE(exit_parameter.is_poisoned());
        ConstInstruction middle_constant =
            middle->instruction_at(0).as<ConstInstruction>();
        ConstInstruction exit_constant =
            exit->instruction_at(0).as<ConstInstruction>();
        EXPECT_EQ(Value::True(), middle_constant.constant());
        EXPECT_EQ(Value::True(), exit_constant.constant());
        EXPECT_EQ(exit_constant.id(), exit->instruction_at(1)
                                          .as<BareReturnInstruction>()
                                          .return_value()
                                          .instruction_id());
    }

    TEST(JitConstantFolding, FoldsProducersAndJoinExposedConsumers)
    {
        constexpr uint64_t input_bits = 0x3ff8000000000000;
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();

        ConstF64Instruction constant =
            builder.emplace_instruction<ConstF64Instruction>(entry, input_bits);
        NegF64Instruction producer =
            builder.emplace_instruction<NegF64Instruction>(entry,
                                                           F64Ref(constant));
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(producer)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            entry, builder.make_block_edge(entry, exit, arguments));

        ParameterF64Instruction parameter =
            builder.emplace_parameter<ParameterF64Instruction>(exit);
        NegF64Instruction consumer =
            builder.emplace_instruction<NegF64Instruction>(exit,
                                                           F64Ref(parameter));
        BoxF64Instruction boxed =
            builder.emplace_instruction<BoxF64Instruction>(exit,
                                                           F64Ref(consumer));
        builder.emplace_instruction<BareReturnInstruction>(
            exit, TaggedValueRef(boxed));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_TRUE(std::move(folding).value());
        EXPECT_TRUE(producer.is_poisoned());
        EXPECT_TRUE(parameter.is_poisoned());
        EXPECT_TRUE(consumer.is_poisoned());
        ConstF64Instruction result =
            exit->instruction_at(1).as<ConstF64Instruction>();
        EXPECT_EQ(input_bits, result.bits());
        EXPECT_EQ(result.id(), exit->instruction_at(2)
                                   .as<BoxF64Instruction>()
                                   .source()
                                   .instruction_id());
    }

    TEST(JitConstantFolding, FoldsF64JoinsOnlyForIdenticalBits)
    {
        constexpr uint64_t bits = 0x8000000000000000;
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ConstF64Instruction first =
            builder.emplace_instruction<ConstF64Instruction>(entry, bits);
        ConstF64Instruction second =
            builder.emplace_instruction<ConstF64Instruction>(entry, bits);
        ConstInstruction condition =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        std::array<ProgramValueRef, 1> first_arguments = {
            ProgramValueRef(first)};
        std::array<ProgramValueRef, 1> second_arguments = {
            ProgramValueRef(second)};
        BlockEdge *first_edge =
            builder.make_block_edge(entry, exit, first_arguments);
        BlockEdge *second_edge =
            builder.make_block_edge(entry, exit, second_arguments);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(condition), first_edge, second_edge);

        ParameterF64Instruction parameter =
            builder.emplace_parameter<ParameterF64Instruction>(exit);
        BoxF64Instruction boxed =
            builder.emplace_instruction<BoxF64Instruction>(exit,
                                                           F64Ref(parameter));
        builder.emplace_instruction<BareReturnInstruction>(
            exit, TaggedValueRef(boxed));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_TRUE(std::move(folding).value());
        EXPECT_TRUE(parameter.is_poisoned());
        ConstF64Instruction materialized =
            exit->instruction_at(0).as<ConstF64Instruction>();
        EXPECT_EQ(bits, materialized.bits());
        EXPECT_EQ(materialized.id(), exit->instruction_at(1)
                                         .as<BoxF64Instruction>()
                                         .source()
                                         .instruction_id());
    }

    TEST(JitConstantFolding, RejectsF64JoinsWithDifferentBits)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ConstF64Instruction positive_zero =
            builder.emplace_instruction<ConstF64Instruction>(
                entry, uint64_t{0x0000000000000000});
        ConstF64Instruction negative_zero =
            builder.emplace_instruction<ConstF64Instruction>(
                entry, uint64_t{0x8000000000000000});
        ConstInstruction condition =
            builder.emplace_instruction<ConstInstruction>(entry, Value::True());
        std::array<ProgramValueRef, 1> first_arguments = {
            ProgramValueRef(positive_zero)};
        std::array<ProgramValueRef, 1> second_arguments = {
            ProgramValueRef(negative_zero)};
        BlockEdge *first_edge =
            builder.make_block_edge(entry, exit, first_arguments);
        BlockEdge *second_edge =
            builder.make_block_edge(entry, exit, second_arguments);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(condition), first_edge, second_edge);
        ParameterF64Instruction parameter =
            builder.emplace_parameter<ParameterF64Instruction>(exit);
        BoxF64Instruction boxed =
            builder.emplace_instruction<BoxF64Instruction>(exit,
                                                           F64Ref(parameter));
        builder.emplace_instruction<BareReturnInstruction>(
            exit, TaggedValueRef(boxed));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_FALSE(std::move(folding).value());
        EXPECT_FALSE(parameter.is_poisoned());
    }

    TEST(JitConstantFolding, DoesNotFoldSelfOnlyParameterCycle)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *loop = builder.emplace_block();
        ConstInstruction none =
            builder.emplace_instruction<ConstInstruction>(entry, Value::None());
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(none));

        ParameterInstruction parameter =
            builder.emplace_parameter<ParameterInstruction>(loop);
        std::array<ProgramValueRef, 1> arguments = {ProgramValueRef(parameter)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            loop, builder.make_block_edge(loop, loop, arguments));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_FALSE(std::move(folding).value());
        EXPECT_FALSE(parameter.is_poisoned());
    }

    TEST(JitConstantFolding, RejectsDifferentConstantsAndEntryParameters)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *exit = builder.emplace_block();
        ParameterInstruction entry_parameter =
            builder.emplace_parameter<ParameterInstruction>(entry);
        ConstInstruction first = builder.emplace_instruction<ConstInstruction>(
            entry, Value::from_smi(1));
        ConstInstruction second = builder.emplace_instruction<ConstInstruction>(
            entry, Value::from_smi(2));
        std::array<ProgramValueRef, 1> first_arguments = {
            ProgramValueRef(first)};
        std::array<ProgramValueRef, 1> second_arguments = {
            ProgramValueRef(second)};
        BlockEdge *first_edge =
            builder.make_block_edge(entry, exit, first_arguments);
        BlockEdge *second_edge =
            builder.make_block_edge(entry, exit, second_arguments);
        builder.emplace_instruction<ConditionalBranchInstruction>(
            entry, TaggedValueRef(entry_parameter), first_edge, second_edge);

        ParameterInstruction exit_parameter =
            builder.emplace_parameter<ParameterInstruction>(exit);
        builder.emplace_instruction<BareReturnInstruction>(
            exit, TaggedValueRef(exit_parameter));
        ControlFlowGraph *graph = builder.finalize();

        auto folding = fold_constants(session, *graph);

        ASSERT_TRUE(folding);
        EXPECT_FALSE(std::move(folding).value());
        EXPECT_FALSE(entry_parameter.is_poisoned());
        EXPECT_FALSE(exit_parameter.is_poisoned());
    }

}  // namespace cl::jit
