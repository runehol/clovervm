#ifndef CL_JIT_GRAPH_REWRITER_H
#define CL_JIT_GRAPH_REWRITER_H

#include "jit/compilation_arena.h"
#include "jit/graph_queries.h"
#include "jit/instruction_traversal.h"

#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace cl::jit
{
    enum class RewriteInput : uint8_t
    {
        Original,
        Normalized,
    };

    class RewriteContext
    {
    public:
        template <typename T, typename... Args>
        T *make_instruction(Args &&...args)
        {
            T *instruction =
                arena_->make_instruction<T>(std::forward<Args>(args)...);
            allocated_instructions_->insert(instruction);
            return instruction;
        }

    private:
        friend class GraphRewriter;

        RewriteContext(
            CompilationArena *arena,
            absl::flat_hash_set<const Instruction *> *allocated_instructions)
            : arena_(arena), allocated_instructions_(allocated_instructions)
        {
        }

        CompilationArena *arena_;
        absl::flat_hash_set<const Instruction *> *allocated_instructions_;
    };

    class RewriteResult
    {
    public:
        using InstructionSequence = absl::InlinedVector<Instruction *, 2>;

        static RewriteResult keep() { return RewriteResult(Kind::Keep); }

        static RewriteResult erase() { return RewriteResult(Kind::Erase); }

        static RewriteResult keep_with_prefix(InstructionSequence instructions)
        {
            return RewriteResult(Kind::KeepWithPrefix, std::move(instructions));
        }

        static RewriteResult keep_with_suffix(InstructionSequence instructions)
        {
            return RewriteResult(Kind::KeepWithSuffix, std::move(instructions));
        }

        static RewriteResult replace(Instruction *instruction)
        {
            assert(instruction != nullptr);
            return RewriteResult(Kind::Replace, {instruction}, instruction);
        }

        static RewriteResult replace(InstructionSequence instructions,
                                     ProgramValueRef result)
        {
            return RewriteResult(Kind::Replace, std::move(instructions),
                                 result.instruction());
        }

        static RewriteResult replace(InstructionSequence instructions,
                                     SnapshotRef result)
        {
            return RewriteResult(Kind::Replace, std::move(instructions),
                                 result.instruction());
        }

        static RewriteResult
        replace_without_result(InstructionSequence instructions)
        {
            return RewriteResult(Kind::ReplaceWithoutResult,
                                 std::move(instructions));
        }

        static RewriteResult replace_with_def(ProgramValueRef def)
        {
            return RewriteResult(Kind::ReplaceWithDef, {}, def.instruction());
        }

        static RewriteResult replace_with_def(SnapshotRef def)
        {
            return RewriteResult(Kind::ReplaceWithDef, {}, def.instruction());
        }

    private:
        friend class GraphRewriter;

        enum class Kind : uint8_t
        {
            Keep,
            KeepWithPrefix,
            KeepWithSuffix,
            Erase,
            Replace,
            ReplaceWithoutResult,
            ReplaceWithDef,
        };

        RewriteResult(Kind kind, InstructionSequence instructions = {},
                      Instruction *replacement_def = nullptr)
            : kind_(kind), instructions_(std::move(instructions)),
              replacement_def_(replacement_def)
        {
        }

        Kind kind_;
        InstructionSequence instructions_;
        Instruction *replacement_def_;
    };

    struct RewriteSummary
    {
        bool instructions_changed = false;
        bool terminators_changed = false;
    };

    class GraphRewriter
    {
    public:
        GraphRewriter(CompilationArena &arena, ControlFlowGraph &graph)
            : arena_(&arena), graph_(&graph)
        {
        }

        template <typename Callback>
        RewriteSummary rewrite_instructions(InstructionTraversal traversal,
                                            Callback &&callback)
        {
            return rewrite_instructions(traversal, RewriteInput::Original,
                                        std::forward<Callback>(callback));
        }

        template <typename Callback>
        RewriteSummary rewrite_instructions(InstructionTraversal traversal,
                                            RewriteInput input,
                                            Callback &&callback)
        {
            using CallbackType = std::remove_reference_t<Callback>;
            auto invoke_callback =
                [](void *opaque, RewriteContext &context,
                   const GraphQueries &queries, const Block &block,
                   const Instruction &instruction) -> RewriteResult {
                return std::invoke(*static_cast<CallbackType *>(opaque),
                                   context, queries, block, instruction);
            };
            return rewrite_instructions_erased(
                traversal, input,
                const_cast<void *>(
                    static_cast<const void *>(std::addressof(callback))),
                invoke_callback);
        }

    private:
        using ErasedCallback = RewriteResult (*)(void *, RewriteContext &,
                                                 const GraphQueries &,
                                                 const Block &,
                                                 const Instruction &);

        RewriteSummary
        rewrite_instructions_erased(InstructionTraversal traversal,
                                    RewriteInput input, void *callback,
                                    ErasedCallback invoke_callback);

        CompilationArena *arena_;
        ControlFlowGraph *graph_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_GRAPH_REWRITER_H
