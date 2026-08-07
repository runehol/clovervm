#include "jit/f64_box_simplification.h"

#include "jit/compilation_session.h"
#include "jit/core_ir_optimization.h"
#include "jit/graph_builder.h"
#include "test_helpers.h"

#include <gtest/gtest.h>

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
