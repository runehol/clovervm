#ifndef CL_JIT_GRAPH_REWRITER_H
#define CL_JIT_GRAPH_REWRITER_H

#include "jit/compilation_session.h"
#include "jit/graph_queries.h"
#include "jit/instruction_traversal.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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
        T make_instruction(Args &&...args)
        {
            T instruction =
                storage_->make_instruction<T>(std::forward<Args>(args)...);
            allocated_instructions_->insert(instruction.id());
            return instruction;
        }

        template <typename T> T retain_and_pin_value(T value)
        {
            return session_->retain_and_pin_value(value);
        }

        Instruction instruction(InstructionId id) const
        {
            return storage_->instruction(id);
        }

        SideExitId
        emplace_side_exit(std::span<const ProgramValueRef> inputs,
                          std::span<const InstructionId> instructions);

    private:
        friend class GraphRewriter;

        RewriteContext(
            CompilationSession *session, CompilationStorage *storage,
            ControlFlowGraph *graph,
            absl::flat_hash_set<InstructionId> *allocated_instructions)
            : session_(session), storage_(storage), graph_(graph),
              allocated_instructions_(allocated_instructions)
        {
        }

        CompilationSession *session_;
        CompilationStorage *storage_;
        ControlFlowGraph *graph_;
        absl::flat_hash_set<InstructionId> *allocated_instructions_;
    };

    class RewriteInsertion
    {
    public:
        using InstructionSequence = absl::InlinedVector<Instruction, 2>;

        class TransferOutput
        {
        public:
            TransferOutput(ProgramValueRef source, ProgramValueRef output)
                : source_(source.instruction_id()),
                  output_(output.instruction_id())
            {
            }

            TransferOutput(SnapshotRef source, SnapshotRef output)
                : source_(source.instruction_id()),
                  output_(output.instruction_id())
            {
            }

            InstructionId source() const { return source_; }
            InstructionId output() const { return output_; }

        private:
            InstructionId source_;
            InstructionId output_;
        };

        using TransferOutputs = absl::InlinedVector<TransferOutput, 2>;

        static RewriteInsertion none() { return RewriteInsertion(); }

        static RewriteInsertion insert(InstructionSequence instructions)
        {
            return RewriteInsertion(std::move(instructions), {});
        }

        static RewriteInsertion
        insert_transfers(InstructionSequence instructions,
                         TransferOutputs outputs)
        {
            return RewriteInsertion(std::move(instructions),
                                    std::move(outputs));
        }

    private:
        friend class GraphRewriter;

        RewriteInsertion() = default;

        RewriteInsertion(InstructionSequence instructions,
                         TransferOutputs outputs)
            : instructions_(std::move(instructions)),
              transfer_outputs_(std::move(outputs))
        {
        }

        InstructionSequence instructions_;
        TransferOutputs transfer_outputs_;
    };

    class BlockParameterRewrite
    {
    public:
        static BlockParameterRewrite keep()
        {
            return BlockParameterRewrite(Kind::Keep);
        }

        static BlockParameterRewrite erase()
        {
            return BlockParameterRewrite(Kind::Erase);
        }

    private:
        friend class GraphRewriter;

        enum class Kind : uint8_t
        {
            Keep,
            Erase,
        };

        explicit BlockParameterRewrite(Kind kind) : kind_(kind) {}

        Kind kind_;
    };

    class RewriteResult
    {
    public:
        using InstructionSequence = absl::InlinedVector<Instruction, 2>;

        static RewriteResult keep() { return RewriteResult(Kind::Keep); }

        static RewriteResult erase() { return RewriteResult(Kind::Erase); }

        static RewriteResult detach() { return RewriteResult(Kind::Detach); }

        static RewriteResult keep_with_prefix(InstructionSequence instructions)
        {
            return RewriteResult(Kind::KeepWithPrefix, std::move(instructions));
        }

        static RewriteResult keep_with_suffix(InstructionSequence instructions)
        {
            return RewriteResult(Kind::KeepWithSuffix, std::move(instructions));
        }

        static RewriteResult replace(Instruction instruction)
        {
            return RewriteResult(Kind::Replace, {instruction},
                                 instruction.id());
        }

        static RewriteResult replace(InstructionSequence instructions,
                                     ProgramValueRef result)
        {
            return RewriteResult(Kind::Replace, std::move(instructions),
                                 result.instruction_id());
        }

        static RewriteResult replace(InstructionSequence instructions,
                                     SnapshotRef result)
        {
            return RewriteResult(Kind::Replace, std::move(instructions),
                                 result.instruction_id());
        }

        static RewriteResult
        replace_without_result(InstructionSequence instructions)
        {
            return RewriteResult(Kind::ReplaceWithoutResult,
                                 std::move(instructions));
        }

        static RewriteResult replace_with_def(ProgramValueRef def)
        {
            return RewriteResult(Kind::ReplaceWithDef, {},
                                 def.instruction_id());
        }

        static RewriteResult replace_with_def(SnapshotRef def)
        {
            return RewriteResult(Kind::ReplaceWithDef, {},
                                 def.instruction_id());
        }

    private:
        friend class GraphRewriter;

        enum class Kind : uint8_t
        {
            Keep,
            KeepWithPrefix,
            KeepWithSuffix,
            Erase,
            Detach,
            Replace,
            ReplaceWithoutResult,
            ReplaceWithDef,
        };

        RewriteResult(
            Kind kind, InstructionSequence instructions = {},
            std::optional<InstructionId> replacement_def = std::nullopt)
            : kind_(kind), instructions_(std::move(instructions)),
              replacement_def_(replacement_def)
        {
        }

        Kind kind_;
        InstructionSequence instructions_;
        std::optional<InstructionId> replacement_def_;
    };

    using NormalizationRemapping =
        absl::flat_hash_map<InstructionId, InstructionId>;

    struct RewriteSummary
    {
        bool block_parameters_changed = false;
        bool instructions_changed = false;
        bool terminators_changed = false;
        bool ir_level_changed = false;
        NormalizationRemapping normalization_remapping;
    };

    class GraphRewriter
    {
    public:
        GraphRewriter(CompilationSession &session, ControlFlowGraph &graph)
            : session_(&session), storage_(session.storage()), graph_(&graph),
              target_ir_level_(graph.ir_level())
        {
            assert(graph_->storage() == storage_);
        }

        void set_target_ir_level(IRLevel target) { target_ir_level_ = target; }

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
            constexpr bool IsInstructionCallback =
                std::is_invocable_r_v<RewriteResult, CallbackType &,
                                      RewriteContext &, const GraphQueries &,
                                      const Block &, const Instruction &>;
            constexpr bool HasBlockEntryCallback =
                requires(CallbackType &candidate, RewriteContext &context,
                         const GraphQueries &queries, const Block &block) {
                    {
                        candidate.at_block_entry(context, queries, block)
                    } -> std::same_as<RewriteInsertion>;
                };
            constexpr bool HasBlockParameterCallback =
                requires(CallbackType &candidate, RewriteContext &context,
                         const GraphQueries &queries, const Block &block,
                         size_t parameter_index, const Instruction &parameter) {
                    {
                        candidate.block_parameter(context, queries, block,
                                                  parameter_index, parameter)
                    } -> std::same_as<BlockParameterRewrite>;
                };
            constexpr bool HasBeforeInstructionCallback =
                requires(CallbackType &candidate, RewriteContext &context,
                         const GraphQueries &queries, const Block &block,
                         const Instruction &instruction) {
                    {
                        candidate.before_instruction(context, queries, block,
                                                     instruction)
                    } -> std::same_as<RewriteInsertion>;
                };
            constexpr bool HasInstructionMethod =
                requires(CallbackType &candidate, RewriteContext &context,
                         const GraphQueries &queries, const Block &block,
                         const Instruction &instruction) {
                    {
                        candidate.rewrite_instruction(context, queries, block,
                                                      instruction)
                    } -> std::same_as<RewriteResult>;
                };

            static_assert(
                IsInstructionCallback || HasBlockParameterCallback ||
                    HasBlockEntryCallback || HasBeforeInstructionCallback ||
                    HasInstructionMethod,
                "a JIT rewrite callback has no supported callback operation");
            static_assert(
                !IsInstructionCallback ||
                    (!HasBlockParameterCallback && !HasBlockEntryCallback &&
                     !HasBeforeInstructionCallback && !HasInstructionMethod),
                "a JIT rewrite callback must be either a callable or a "
                "callback object");

            ErasedCallbacks callbacks;
            if constexpr(IsInstructionCallback)
            {
                callbacks.rewrite_instruction =
                    [](void *opaque, RewriteContext &context,
                       const GraphQueries &queries, const Block &block,
                       const Instruction &instruction) -> RewriteResult {
                    return std::invoke(*static_cast<CallbackType *>(opaque),
                                       context, queries, block, instruction);
                };
            }
            else
            {
                if constexpr(HasBlockParameterCallback)
                {
                    callbacks.block_parameter =
                        [](void *opaque, RewriteContext &context,
                           const GraphQueries &queries, const Block &block,
                           size_t parameter_index, const Instruction &parameter)
                        -> BlockParameterRewrite {
                        return static_cast<CallbackType *>(opaque)
                            ->block_parameter(context, queries, block,
                                              parameter_index, parameter);
                    };
                }
                if constexpr(HasBlockEntryCallback)
                {
                    callbacks.at_block_entry =
                        [](void *opaque, RewriteContext &context,
                           const GraphQueries &queries,
                           const Block &block) -> RewriteInsertion {
                        return static_cast<CallbackType *>(opaque)
                            ->at_block_entry(context, queries, block);
                    };
                }
                if constexpr(HasBeforeInstructionCallback)
                {
                    callbacks.before_instruction =
                        [](void *opaque, RewriteContext &context,
                           const GraphQueries &queries, const Block &block,
                           const Instruction &instruction) -> RewriteInsertion {
                        return static_cast<CallbackType *>(opaque)
                            ->before_instruction(context, queries, block,
                                                 instruction);
                    };
                }
                if constexpr(HasInstructionMethod)
                {
                    callbacks.rewrite_instruction =
                        [](void *opaque, RewriteContext &context,
                           const GraphQueries &queries, const Block &block,
                           const Instruction &instruction) -> RewriteResult {
                        return static_cast<CallbackType *>(opaque)
                            ->rewrite_instruction(context, queries, block,
                                                  instruction);
                    };
                }
            }

            return this->template rewrite_instructions_erased <
                       HasBlockParameterCallback,
                   HasBlockEntryCallback, HasBeforeInstructionCallback,
                   IsInstructionCallback ||
                       HasInstructionMethod >
                           (traversal, input,
                            const_cast<void *>(static_cast<const void *>(
                                std::addressof(callback))),
                            callbacks);
        }

    private:
        struct ErasedCallbacks
        {
            BlockParameterRewrite (*block_parameter)(
                void *, RewriteContext &, const GraphQueries &, const Block &,
                size_t, const Instruction &) = nullptr;
            RewriteInsertion (*at_block_entry)(void *, RewriteContext &,
                                               const GraphQueries &,
                                               const Block &) = nullptr;
            RewriteInsertion (*before_instruction)(
                void *, RewriteContext &, const GraphQueries &, const Block &,
                const Instruction &) = nullptr;
            RewriteResult (*rewrite_instruction)(void *, RewriteContext &,
                                                 const GraphQueries &,
                                                 const Block &,
                                                 const Instruction &) = nullptr;
        };

        template <bool HasBlockParameterCallback, bool HasBlockEntryCallback,
                  bool HasBeforeInstructionCallback,
                  bool HasInstructionCallback>
        RewriteSummary
        rewrite_instructions_erased(InstructionTraversal traversal,
                                    RewriteInput input, void *callback,
                                    const ErasedCallbacks &callbacks);

        CompilationSession *session_;
        CompilationStorage *storage_;
        ControlFlowGraph *graph_;
        IRLevel target_ir_level_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_GRAPH_REWRITER_H
