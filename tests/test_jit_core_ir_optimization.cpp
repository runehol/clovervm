#include "jit/core_ir_optimization.h"

#include "builtin_types/float.h"
#include "jit/compilation_session.h"
#include "jit/graph_builder.h"
#include "object_model/owned.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <utility>

namespace cl::jit
{
    TEST(JitCoreIROptimization, RunsConstantFoldingInTheCorePipeline)
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

        auto optimization = optimize_core_ir(session, *graph);

        ASSERT_TRUE(optimization);
        EXPECT_TRUE(std::move(optimization).value());
        ASSERT_EQ(3u, entry->instructions().size());
        ConstF64Instruction folded =
            entry->instruction_at(0).as<ConstF64Instruction>();
        EXPECT_EQ(std::bit_cast<uint64_t>(-std::bit_cast<double>(input_bits)),
                  folded.bits());
        EXPECT_EQ(folded.id(), entry->instruction_at(1)
                                   .as<BoxF64Instruction>()
                                   .source()
                                   .instruction_id());
    }

    TEST(JitCoreIROptimization, RepeatsAfterEquivalentParametersRemoveABoxAlias)
    {
        CompilationSession session{test::compiler_thread()};
        GraphBuilder builder(session, IRLevel::Core);
        Block *entry = builder.emplace_block();
        Block *destination = builder.emplace_block();
        ParameterF64Instruction source =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            entry, F64Ref(source));
        std::array<ProgramValueRef, 2> arguments = {ProgramValueRef(box),
                                                    ProgramValueRef(box)};
        builder.emplace_instruction<UnconditionalBranchInstruction>(
            entry, builder.make_block_edge(entry, destination, arguments));
        ParameterInstruction first =
            builder.emplace_parameter<ParameterInstruction>(destination);
        ParameterInstruction second =
            builder.emplace_parameter<ParameterInstruction>(destination);
        IsInstruction same = builder.emplace_instruction<IsInstruction>(
            destination, TaggedValueRef(first), TaggedValueRef(second));
        builder.emplace_instruction<BareReturnInstruction>(
            destination, TaggedValueRef(same));
        ControlFlowGraph *graph = builder.finalize();

        auto optimization = optimize_core_ir(session, *graph);

        ASSERT_TRUE(optimization);
        EXPECT_TRUE(std::move(optimization).value());
        EXPECT_TRUE(first.is_poisoned());
        EXPECT_TRUE(second.is_poisoned());
        ASSERT_EQ(1u, destination->parameters().size());
        ParameterF64Instruction replacement =
            destination->parameter_at(0).as<ParameterF64Instruction>();
        ASSERT_EQ(3u, destination->instructions().size());
        BoxF64Instruction destination_box =
            destination->instruction_at(0).as<BoxF64Instruction>();
        EXPECT_EQ(replacement.id(), destination_box.source().instruction_id());
        IsInstruction rewritten_is =
            destination->instruction_at(1).as<IsInstruction>();
        EXPECT_EQ(destination_box.id(), rewritten_is.lhs().instruction_id());
        EXPECT_EQ(destination_box.id(), rewritten_is.rhs().instruction_id());
        EXPECT_EQ(
            source.id(),
            entry->block_successor_edges()[0]->arguments()[0].instruction_id());
    }

}  // namespace cl::jit
