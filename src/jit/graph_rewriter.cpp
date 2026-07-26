#include "jit/graph_rewriter.h"

#include "jit/cfg_verifier.h"
#include "jit/instruction_reconstruction.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <cassert>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        void require_rewrite_invariant(bool condition, const char *message)
        {
            if(!condition)
            {
                fatal(message);
            }
        }

        struct DefReplacement
        {
            std::optional<InstructionId> def;
            bool erased;
        };

        using DefReplacements =
            absl::flat_hash_map<InstructionId, DefReplacement>;
        using SequenceReplacements =
            absl::flat_hash_map<InstructionId, InstructionId>;
        using EdgeReplacements =
            absl::flat_hash_map<const BlockEdge *, BlockEdge *>;

        class DefResolver
        {
        public:
            DefResolver(const DefReplacements &def_replacements,
                        const SequenceReplacements &sequence_replacements,
                        const EdgeReplacements &edge_replacements)
                : def_replacements_(&def_replacements),
                  sequence_replacements_(&sequence_replacements),
                  edge_replacements_(&edge_replacements)
            {
            }

            InstructionId resolve(InstructionId def) const
            {
                auto sequence = sequence_replacements_->find(def);
                if(sequence != sequence_replacements_->end())
                {
                    return sequence->second;
                }

                auto replacement = def_replacements_->find(def);
                if(replacement == def_replacements_->end())
                {
                    return def;
                }
                require_rewrite_invariant(
                    !replacement->second.erased,
                    "JIT rewrite uses an erased definition");
                require_rewrite_invariant(
                    replacement->second.def.has_value(),
                    "JIT rewrite resolved a definition to null");
                return *replacement->second.def;
            }

            BlockEdge *resolve(BlockEdge *edge) const
            {
                auto replacement = edge_replacements_->find(edge);
                if(replacement == edge_replacements_->end())
                {
                    return edge;
                }
                return replacement->second;
            }

        private:
            const DefReplacements *def_replacements_;
            const SequenceReplacements *sequence_replacements_;
            const EdgeReplacements *edge_replacements_;
        };

        bool compatible_results(const Instruction &original,
                                const Instruction &replacement)
        {
            if(original.result_class() != replacement.result_class())
            {
                return false;
            }
            return original.result_class() != ResultClass::ProgramValue ||
                   original.value_representation() ==
                       replacement.value_representation();
        }

        bool same_successor_edges(const Instruction &original,
                                  const Instruction &replacement,
                                  const EdgeReplacements &edge_replacements)
        {
            auto original_edges =
                TerminatorInstruction(original).block_successor_edges();
            auto replacement_edges =
                TerminatorInstruction(replacement).block_successor_edges();
            if(original_edges.size() != replacement_edges.size())
            {
                return false;
            }
            for(size_t index = 0; index < original_edges.size(); ++index)
            {
                auto found = edge_replacements.find(original_edges[index]);
                BlockEdge *expected = found == edge_replacements.end()
                                          ? original_edges[index]
                                          : found->second;
                if(replacement_edges[index] != expected)
                {
                    return false;
                }
            }
            return true;
        }

        void validate_available_operands(
            const CompilationStorage &storage, const Instruction &instruction,
            const absl::flat_hash_set<InstructionId> &available_defs)
        {
            visit_operand_references(
                instruction, [&](uint32_t, OperandClass operand_class,
                                 ValueRepresentation required_representation,
                                 InstructionId definition_id) {
                    Instruction def = storage.instruction(definition_id);
                    require_rewrite_invariant(
                        available_defs.contains(def.id()),
                        "rewritten instruction refers to a definition outside "
                        "its block or before its definition");
                    require_rewrite_invariant(
                        static_cast<uint8_t>(operand_class) ==
                            static_cast<uint8_t>(def.result_class()),
                        "rewritten instruction has an operand with an "
                        "incompatible result class");
                    if(operand_class == OperandClass::ProgramValue &&
                       required_representation != ValueRepresentation::None)
                    {
                        require_rewrite_invariant(
                            def.value_representation() ==
                                required_representation,
                            "rewritten instruction has an operand with an "
                            "incompatible value representation");
                    }
                });
        }

        struct StagedBlockRewrite
        {
            Block *block;
            std::vector<InstructionId> parameters;
            std::vector<InstructionId> instructions;
            std::vector<InstructionId> removed_originals;
        };

        using ParameterRetentionMasks =
            absl::flat_hash_map<const Block *, std::vector<bool>>;
    }  // namespace

    template <bool HasBlockParameterCallback, bool HasBlockEntryCallback,
              bool HasBeforeInstructionCallback, bool HasInstructionCallback>
    RewriteSummary GraphRewriter::rewrite_instructions_erased(
        InstructionTraversal traversal, RewriteInput input, void *callback,
        const ErasedCallbacks &callbacks)
    {
        assert(graph_ != nullptr);
        require_rewrite_invariant(graph_->is_published(),
                                  "cannot rewrite an unpublished JIT CFG");
        assert(traversal.block_order() == BlockWalkOrder::ProgramOrder);
        require_rewrite_invariant(
            input != RewriteInput::Normalized ||
                !has_graph_query(traversal.queries(), GraphQuery::Uses),
            "normalized rewrite input cannot be combined with use lists");
        assert(callback != nullptr);
        if constexpr(HasBlockEntryCallback)
        {
            assert(callbacks.at_block_entry != nullptr);
        }
        if constexpr(HasBlockParameterCallback)
        {
            assert(callbacks.block_parameter != nullptr);
        }
        if constexpr(HasBeforeInstructionCallback)
        {
            assert(callbacks.before_instruction != nullptr);
        }
        if constexpr(HasInstructionCallback)
        {
            assert(callbacks.rewrite_instruction != nullptr);
        }

        GraphQueries queries = graph_->prepare_queries(traversal.queries());
        absl::flat_hash_set<InstructionId> allocated_instructions;
        RewriteContext context(session_, storage_, &allocated_instructions);
        RewriteSummary summary;
        ParameterRetentionMasks parameter_retention;
        if constexpr(HasBlockParameterCallback)
        {
            parameter_retention.reserve(graph_->blocks_.size());
            for(const Block *block: graph_->blocks_)
            {
                std::vector<bool> retained;
                retained.reserve(block->parameters_.size());
                for(size_t index = 0; index < block->parameters_.size();
                    ++index)
                {
                    BlockParameterRewrite rewrite = callbacks.block_parameter(
                        callback, context, queries, *block, index,
                        storage_->instruction(block->parameters_[index]));
                    bool keep =
                        rewrite.kind_ == BlockParameterRewrite::Kind::Keep;
                    retained.push_back(keep);
                    summary.block_parameters_changed |= !keep;
                }
                parameter_retention.emplace(block, std::move(retained));
            }
        }

        absl::flat_hash_set<InstructionId> staged_instruction_set;
        std::vector<StagedBlockRewrite> staged_blocks;
        staged_blocks.reserve(graph_->blocks_.size());
        bool edges_changed = false;
        auto record_normalization = [&](InstructionId before,
                                        InstructionId after) {
            if(before == after)
            {
                return;
            }
            auto [position, inserted] =
                summary.normalization_remapping.emplace(before, after);
            require_rewrite_invariant(
                inserted || position->second == after,
                "a JIT rewrite instruction has more than one normalized "
                "identity");
        };

        for(Block *block: graph_->blocks_)
        {
            assert(block != nullptr);
            StagedBlockRewrite staged{block, {}, {}, {}};
            staged.parameters.reserve(block->parameters_.size());
            staged.instructions.reserve(block->instructions_.size());
            if constexpr(HasBlockParameterCallback)
            {
                const std::vector<bool> &retained =
                    parameter_retention.at(block);
                for(size_t index = 0; index < block->parameters_.size();
                    ++index)
                {
                    InstructionId parameter_id = block->parameters_[index];
                    if(retained[index])
                    {
                        staged.parameters.push_back(parameter_id);
                    }
                    else
                    {
                        staged.removed_originals.push_back(parameter_id);
                    }
                }
            }
            else
            {
                staged.parameters = block->parameters_;
            }
            DefReplacements def_replacements;
            EdgeReplacements edge_replacements;
            absl::flat_hash_set<InstructionId> available_defs;
            for(InstructionId parameter: staged.parameters)
            {
                available_defs.insert(parameter);
            }

            auto process_insertion = [&](RewriteInsertion insertion) {
                absl::flat_hash_set<InstructionId> transfer_sources;
                SequenceReplacements no_sequence_replacements;
                DefResolver existing_resolver(def_replacements,
                                              no_sequence_replacements,
                                              edge_replacements);
                for(const RewriteInsertion::TransferOutput &transfer:
                    insertion.transfer_outputs_)
                {
                    require_rewrite_invariant(
                        transfer_sources.insert(transfer.source()).second,
                        "JIT rewrite insertion transfers one source more than "
                        "once");
                    InstructionId active_source =
                        existing_resolver.resolve(transfer.source());
                    require_rewrite_invariant(
                        available_defs.contains(active_source),
                        "JIT rewrite insertion transfers a source not "
                        "available at the insertion point");
                }

                SequenceReplacements sequence_replacements;
                for(Instruction proposed: insertion.instructions_)
                {
                    require_rewrite_invariant(
                        allocated_instructions.contains(proposed.id()),
                        "inserted instructions must be allocated through this "
                        "rewrite's context");
                    require_rewrite_invariant(
                        !sequence_replacements.contains(proposed.id()),
                        "a rewrite insertion may not emit one instruction "
                        "twice");

                    DefResolver resolver(def_replacements,
                                         sequence_replacements,
                                         edge_replacements);
                    Instruction normalized =
                        rebuild_instruction_with_references(proposed, *storage_,
                                                            resolver, context);
                    require_rewrite_invariant(
                        !normalized.is_detached(),
                        "a detached instruction cannot be inserted");
                    require_rewrite_invariant(
                        !is_block_parameter_kind(normalized.kind()),
                        "block-parameter instructions cannot be inserted into "
                        "a block body");
                    require_rewrite_invariant(
                        !normalized.is_block_terminator(),
                        "a block terminator cannot be structurally inserted");
                    require_rewrite_invariant(
                        staged_instruction_set.insert(normalized.id()).second,
                        "an instruction cannot occupy more than one graph "
                        "position");
                    sequence_replacements.emplace(proposed.id(),
                                                  normalized.id());
                    record_normalization(proposed.id(), normalized.id());
                    validate_available_operands(*storage_, normalized,
                                                available_defs);
                    staged.instructions.push_back(normalized.id());
                    if(normalized.result_class() != ResultClass::None)
                    {
                        available_defs.insert(normalized.id());
                    }
                }

                for(const RewriteInsertion::TransferOutput &transfer:
                    insertion.transfer_outputs_)
                {
                    Instruction source =
                        storage_->instruction(transfer.source());
                    auto emitted =
                        sequence_replacements.find(transfer.output());
                    require_rewrite_invariant(
                        emitted != sequence_replacements.end(),
                        "a rewrite insertion transfer output must be emitted "
                        "by that insertion");
                    require_rewrite_invariant(
                        compatible_results(
                            source, storage_->instruction(emitted->second)),
                        "a rewrite insertion transfer has an incompatible "
                        "result class or value representation");
                    def_replacements.insert_or_assign(
                        transfer.source(),
                        DefReplacement{emitted->second, false});
                }

                if(!insertion.instructions_.empty())
                {
                    summary.instructions_changed = true;
                }
            };

            if constexpr(HasBlockEntryCallback)
            {
                process_insertion(callbacks.at_block_entry(callback, context,
                                                           queries, *block));
            }

            for(InstructionId original_id: block->instructions_)
            {
                Instruction original = storage_->instruction(original_id);
                if constexpr(HasBeforeInstructionCallback)
                {
                    process_insertion(callbacks.before_instruction(
                        callback, context, queries, *block, original));
                }
                if(original.is_block_terminator())
                {
                    SequenceReplacements no_sequence_replacements;
                    DefResolver resolver(def_replacements,
                                         no_sequence_replacements,
                                         edge_replacements);
                    for(BlockEdge *edge:
                        TerminatorInstruction(original).block_successor_edges())
                    {
                        std::vector<ProgramValueRef> arguments;
                        arguments.reserve(edge->arguments().size());
                        bool changed = false;
                        const std::vector<bool> *retained_arguments = nullptr;
                        if constexpr(HasBlockParameterCallback)
                        {
                            retained_arguments =
                                &parameter_retention.at(edge->target());
                            require_rewrite_invariant(
                                retained_arguments->size() ==
                                    edge->arguments().size(),
                                "JIT rewrite block parameter count does not "
                                "match incoming edge arguments");
                        }
                        for(size_t index = 0; index < edge->arguments().size();
                            ++index)
                        {
                            if constexpr(HasBlockParameterCallback)
                            {
                                if(!(*retained_arguments)[index])
                                {
                                    changed = true;
                                    continue;
                                }
                            }
                            ProgramValueRef argument = edge->arguments()[index];
                            InstructionId resolved =
                                resolver.resolve(argument.instruction_id());
                            require_rewrite_invariant(
                                available_defs.contains(resolved),
                                "rewritten block edge refers to a definition "
                                "outside its source block or after the edge");
                            changed |= resolved != argument.instruction_id();
                            arguments.emplace_back(
                                storage_->instruction(resolved));
                        }
                        BlockEdge *replacement = edge;
                        if(changed)
                        {
                            replacement = storage_->make_block_edge(
                                edge->source(), edge->target(), arguments);
                            edges_changed = true;
                        }
                        edge_replacements.emplace(edge, replacement);
                    }
                }

                Instruction callback_input = original;
                if(input == RewriteInput::Normalized)
                {
                    SequenceReplacements no_sequence_replacements;
                    DefResolver resolver(def_replacements,
                                         no_sequence_replacements,
                                         edge_replacements);
                    callback_input = rebuild_instruction_with_references(
                        original, *storage_, resolver, context);
                    record_normalization(original.id(), callback_input.id());
                    validate_available_operands(*storage_, callback_input,
                                                available_defs);
                }
                RewriteResult result = RewriteResult::keep();
                if constexpr(HasInstructionCallback)
                {
                    result = callbacks.rewrite_instruction(
                        callback, context, queries, *block, callback_input);
                }
                SequenceReplacements sequence_replacements;
                RewriteResult::InstructionSequence proposed_instructions;
                std::optional<InstructionId> proposed_replacement;
                bool replacement_is_existing_def = false;
                bool explicitly_erased = false;

                switch(result.kind_)
                {
                    case RewriteResult::Kind::Keep:
                        proposed_instructions.push_back(callback_input);
                        proposed_replacement =
                            callback_input.result_class() == ResultClass::None
                                ? std::nullopt
                                : std::optional(callback_input.id());
                        break;
                    case RewriteResult::Kind::KeepWithPrefix:
                        proposed_instructions = std::move(result.instructions_);
                        proposed_instructions.push_back(callback_input);
                        proposed_replacement =
                            callback_input.result_class() == ResultClass::None
                                ? std::nullopt
                                : std::optional(callback_input.id());
                        break;
                    case RewriteResult::Kind::KeepWithSuffix:
                        proposed_instructions.push_back(callback_input);
                        proposed_instructions.insert(
                            proposed_instructions.end(),
                            result.instructions_.begin(),
                            result.instructions_.end());
                        proposed_replacement =
                            callback_input.result_class() == ResultClass::None
                                ? std::nullopt
                                : std::optional(callback_input.id());
                        break;
                    case RewriteResult::Kind::Erase:
                        explicitly_erased = true;
                        break;
                    case RewriteResult::Kind::Replace:
                        proposed_instructions = std::move(result.instructions_);
                        assert(result.replacement_def_.has_value());
                        proposed_replacement = *result.replacement_def_;
                        if(original.result_class() == ResultClass::None)
                        {
                            proposed_replacement = std::nullopt;
                        }
                        break;
                    case RewriteResult::Kind::ReplaceWithoutResult:
                        require_rewrite_invariant(
                            original.result_class() == ResultClass::None,
                            "replace_without_result requires an instruction "
                            "without a result");
                        proposed_instructions = std::move(result.instructions_);
                        break;
                    case RewriteResult::Kind::ReplaceWithDef:
                        require_rewrite_invariant(
                            original.result_class() != ResultClass::None,
                            "replace_with_def requires a result-producing "
                            "instruction");
                        assert(result.instructions_.empty());
                        assert(result.replacement_def_.has_value());
                        proposed_replacement = *result.replacement_def_;
                        replacement_is_existing_def = true;
                        break;
                }

                size_t output_start = staged.instructions.size();
                bool keeps_callback_input =
                    result.kind_ == RewriteResult::Kind::Keep ||
                    result.kind_ == RewriteResult::Kind::KeepWithPrefix ||
                    result.kind_ == RewriteResult::Kind::KeepWithSuffix;
                for(Instruction proposed: proposed_instructions)
                {
                    if(!keeps_callback_input ||
                       proposed.id() != callback_input.id())
                    {
                        require_rewrite_invariant(
                            allocated_instructions.contains(proposed.id()),
                            "replacement instructions must be allocated "
                            "through this rewrite's context");
                    }
                    require_rewrite_invariant(
                        !sequence_replacements.contains(proposed.id()),
                        "a replacement sequence may not emit one instruction "
                        "twice");

                    DefResolver resolver(def_replacements,
                                         sequence_replacements,
                                         edge_replacements);
                    Instruction normalized =
                        rebuild_instruction_with_references(proposed, *storage_,
                                                            resolver, context);
                    require_rewrite_invariant(
                        !normalized.is_detached(),
                        "a detached instruction cannot be emitted");
                    require_rewrite_invariant(
                        !is_block_parameter_kind(normalized.kind()),
                        "block-parameter instructions cannot be emitted into a "
                        "block body");
                    require_rewrite_invariant(
                        staged_instruction_set.insert(normalized.id()).second,
                        "an instruction cannot occupy more than one graph "
                        "position");
                    sequence_replacements.emplace(proposed.id(),
                                                  normalized.id());
                    record_normalization(proposed.id(), normalized.id());
                    validate_available_operands(*storage_, normalized,
                                                available_defs);
                    staged.instructions.push_back(normalized.id());
                    if(normalized.result_class() != ResultClass::None)
                    {
                        available_defs.insert(normalized.id());
                    }
                }

                std::optional<InstructionId> normalized_replacement;
                if(proposed_replacement.has_value())
                {
                    DefResolver resolver(def_replacements,
                                         sequence_replacements,
                                         edge_replacements);
                    if(replacement_is_existing_def)
                    {
                        normalized_replacement =
                            resolver.resolve(*proposed_replacement);
                        require_rewrite_invariant(
                            available_defs.contains(*normalized_replacement),
                            "replace_with_def requires a definition already "
                            "available in the staged block");
                    }
                    else
                    {
                        auto replacement =
                            sequence_replacements.find(*proposed_replacement);
                        require_rewrite_invariant(
                            replacement != sequence_replacements.end(),
                            "a replacement result must be emitted by its "
                            "replacement sequence");
                        normalized_replacement = replacement->second;
                    }
                    require_rewrite_invariant(
                        compatible_results(
                            original,
                            storage_->instruction(*normalized_replacement)),
                        "a replacement definition has an incompatible result "
                        "class or value representation");
                }

                if(original.result_class() != ResultClass::None)
                {
                    if(explicitly_erased)
                    {
                        def_replacements.emplace(
                            original.id(), DefReplacement{std::nullopt, true});
                    }
                    else
                    {
                        require_rewrite_invariant(
                            normalized_replacement.has_value(),
                            "a result-producing instruction requires a "
                            "replacement definition");
                        def_replacements.emplace(
                            original.id(),
                            DefReplacement{normalized_replacement, false});
                    }
                }
                else
                {
                    require_rewrite_invariant(
                        !normalized_replacement.has_value(),
                        "an instruction without a result cannot have a "
                        "replacement definition");
                }

                size_t output_count = staged.instructions.size() - output_start;
                bool position_unchanged =
                    output_count == 1 &&
                    staged.instructions[output_start] == original.id();
                bool original_retained = false;
                for(size_t index = output_start;
                    index < staged.instructions.size(); ++index)
                {
                    original_retained |=
                        staged.instructions[index] == original.id();
                }
                if(!original_retained)
                {
                    staged.removed_originals.push_back(original.id());
                }
                if(!position_unchanged)
                {
                    summary.instructions_changed = true;
                }

                bool original_is_terminator = original.is_block_terminator();
                for(size_t index = output_start;
                    index < staged.instructions.size(); ++index)
                {
                    bool is_final_output =
                        index + 1 == staged.instructions.size();
                    bool emitted_terminator =
                        storage_->instruction(staged.instructions[index])
                            .is_block_terminator();
                    if(original_is_terminator)
                    {
                        require_rewrite_invariant(
                            is_final_output || !emitted_terminator,
                            "only the final replacement instruction may be a "
                            "terminator");
                    }
                    else
                    {
                        require_rewrite_invariant(
                            !emitted_terminator,
                            "a non-terminator cannot emit a terminator");
                    }
                }
                if(original_is_terminator)
                {
                    require_rewrite_invariant(output_count != 0,
                                              "a terminator cannot be erased");
                    Instruction new_terminator =
                        storage_->instruction(staged.instructions.back());
                    require_rewrite_invariant(
                        new_terminator.is_block_terminator(),
                        "a terminator replacement must end in a terminator");
                    require_rewrite_invariant(
                        same_successor_edges(original, new_terminator,
                                             edge_replacements),
                        "instruction rewriting cannot change CFG successor "
                        "edges");
                    summary.terminators_changed |=
                        new_terminator.id() != original.id();
                }
            }

            require_rewrite_invariant(!staged.instructions.empty(),
                                      "a rewritten block cannot be empty");
            require_rewrite_invariant(
                storage_->instruction(staged.instructions.back())
                    .is_block_terminator(),
                "a rewritten block must end in a terminator");

            staged_blocks.push_back(std::move(staged));
        }

        if(!summary.block_parameters_changed && !summary.instructions_changed)
        {
            return summary;
        }

        for(StagedBlockRewrite &staged: staged_blocks)
        {
            staged.block->parameters_.swap(staged.parameters);
            staged.block->instructions_.swap(staged.instructions);
        }
        if(edges_changed)
        {
            graph_->rebuild_predecessor_edge_index();
        }
        for(StagedBlockRewrite &staged: staged_blocks)
        {
            for(InstructionId removed: staged.removed_originals)
            {
                storage_->detach_instruction(removed);
            }
        }
        ++graph_->mutation_generation_;

#ifndef NDEBUG
        CfgVerificationResult verification = verify_cfg(*graph_);
        if(!verification.valid)
        {
            fatal("graph rewriter produced an invalid JIT CFG: " +
                  verification.message);
        }
#endif
        return summary;
    }

#define CL_JIT_INSTANTIATE_GRAPH_REWRITER(parameters, entry, before,           \
                                          instruction)                         \
    template RewriteSummary GraphRewriter::rewrite_instructions_erased<        \
        parameters, entry, before, instruction>(                               \
        InstructionTraversal, RewriteInput, void *, const ErasedCallbacks &);

    CL_JIT_INSTANTIATE_GRAPH_REWRITER(false, false, false, true)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(false, false, true, false)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(false, false, true, true)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(false, true, false, false)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(false, true, false, true)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(false, true, true, false)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(false, true, true, true)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(true, false, false, false)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(true, false, false, true)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(true, false, true, false)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(true, false, true, true)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(true, true, false, false)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(true, true, false, true)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(true, true, true, false)
    CL_JIT_INSTANTIATE_GRAPH_REWRITER(true, true, true, true)

#undef CL_JIT_INSTANTIATE_GRAPH_REWRITER

}  // namespace cl::jit
