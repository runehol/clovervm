#include "jit/aarch64_link_register_preservation.h"

#include "jit/aarch64_call.h"
#include "jit/compilation_session.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_builder.h"
#include "jit/graph_rewriter.h"
#include "jit/instruction.h"
#include "object_model/value.h"
#include "runtime/thread_state.h"

#include <gtest/gtest.h>

#include <array>
#include <span>

namespace cl::jit
{
    namespace
    {
        Value test_handler(ThreadState *, Value value) { return value; }
    }  // namespace

    TEST(AArch64CallProperties, ClassifiesBackendCallLowerings)
    {
        EXPECT_FALSE(
            aarch64_call_properties(InstructionKind::AddF64).has_value());

        std::optional<AArch64CallProperties> trusted =
            aarch64_call_properties(InstructionKind::TrustedHandlerCall);
        ASSERT_TRUE(trusted.has_value());
        EXPECT_TRUE(trusted->permits_call_local_spills);

        std::optional<AArch64CallProperties> box =
            aarch64_call_properties(InstructionKind::BoxF64);
        ASSERT_TRUE(box.has_value());
        EXPECT_TRUE(box->permits_call_local_spills);
    }

    TEST(AArch64LinkRegisterPreservation, LeavesLeafGraphUnchanged)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction value =
            builder.emplace_parameter<ParameterInstruction>(entry);
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(value));
        ControlFlowGraph *graph = builder.finalize();

        RewriteSummary summary =
            insert_aarch64_link_register_preservation(session, *graph);

        EXPECT_FALSE(summary.instructions_changed);
        ASSERT_EQ(1u, entry->instructions().size());
        EXPECT_EQ(InstructionKind::BareReturn, entry->instruction_at(0).kind());
    }

    TEST(AArch64LinkRegisterPreservation, SurroundsCallsWithFrameSaveAndRestore)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterInstruction value =
            builder.emplace_parameter<ParameterInstruction>(entry);
        std::array<TaggedValueRef, 1> arguments = {TaggedValueRef(value)};
        TrustedHandlerCallInstruction call =
            builder.emplace_instruction<TrustedHandlerCallInstruction>(
                entry, std::span<const TaggedValueRef>(arguments),
                erase_trusted_handler_target(test_handler));
        builder.emplace_instruction<BareReturnInstruction>(
            entry, TaggedValueRef(call));
        ControlFlowGraph *graph = builder.finalize();

        RewriteSummary summary =
            insert_aarch64_link_register_preservation(session, *graph);

        EXPECT_TRUE(summary.instructions_changed);
        ASSERT_EQ(4u, entry->instructions().size());
        EXPECT_EQ(InstructionKind::SaveLinkRegisterToFrame,
                  entry->instruction_at(0).kind());
        EXPECT_EQ(InstructionKind::TrustedHandlerCall,
                  entry->instruction_at(1).kind());
        EXPECT_EQ(InstructionKind::RestoreLinkRegisterFromFrame,
                  entry->instruction_at(2).kind());
        EXPECT_EQ(InstructionKind::BareReturn, entry->instruction_at(3).kind());
    }

    TEST(AArch64LinkRegisterPreservation, TreatsBoxF64AsANonLeafNativeCall)
    {
        CompilationSession session;
        GraphBuilder builder(session, IRLevel::Machine);
        Block *entry = builder.emplace_block();
        ParameterF64Instruction value =
            builder.emplace_parameter<ParameterF64Instruction>(entry);
        BoxF64Instruction box = builder.emplace_instruction<BoxF64Instruction>(
            entry, F64Ref(value));
        builder.emplace_instruction<BareReturnInstruction>(entry,
                                                           TaggedValueRef(box));
        ControlFlowGraph *graph = builder.finalize();

        RewriteSummary summary =
            insert_aarch64_link_register_preservation(session, *graph);

        EXPECT_TRUE(summary.instructions_changed);
        ASSERT_EQ(4u, entry->instructions().size());
        EXPECT_EQ(InstructionKind::SaveLinkRegisterToFrame,
                  entry->instruction_at(0).kind());
        EXPECT_EQ(InstructionKind::BoxF64, entry->instruction_at(1).kind());
        EXPECT_EQ(InstructionKind::RestoreLinkRegisterFromFrame,
                  entry->instruction_at(2).kind());
        EXPECT_EQ(InstructionKind::BareReturn, entry->instruction_at(3).kind());
    }

}  // namespace cl::jit
