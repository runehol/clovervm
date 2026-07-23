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
            Instruction *def;
            bool erased;
        };

        using DefReplacements =
            absl::flat_hash_map<const Instruction *, DefReplacement>;
        using SequenceReplacements =
            absl::flat_hash_map<const Instruction *, Instruction *>;

        class DefResolver
        {
        public:
            DefResolver(const DefReplacements &def_replacements,
                        const SequenceReplacements &sequence_replacements)
                : def_replacements_(&def_replacements),
                  sequence_replacements_(&sequence_replacements)
            {
            }

            Instruction *resolve(Instruction *def) const
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
                    replacement->second.def != nullptr,
                    "JIT rewrite resolved a definition to null");
                return replacement->second.def;
            }

        private:
            const DefReplacements *def_replacements_;
            const SequenceReplacements *sequence_replacements_;
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

        bool is_parameter_kind(InstructionKind kind)
        {
            return kind == InstructionKind::Parameter ||
                   kind == InstructionKind::ParameterF64;
        }

        bool same_successor_edges(const Instruction &original,
                                  const Instruction &replacement)
        {
            auto original_edges =
                TerminatorInstruction(&original).block_successor_edges();
            auto replacement_edges =
                TerminatorInstruction(&replacement).block_successor_edges();
            return original_edges == replacement_edges;
        }

        void validate_available_operands(
            const Instruction &instruction,
            const absl::flat_hash_set<const Instruction *> &available_defs)
        {
            visit_operand_references(
                instruction, [&](uint32_t, OperandClass operand_class,
                                 ValueRepresentation required_representation,
                                 Instruction *def) {
                    require_rewrite_invariant(
                        available_defs.contains(def),
                        "rewritten instruction refers to a definition outside "
                        "its block or before its definition");
                    require_rewrite_invariant(
                        static_cast<uint8_t>(operand_class) ==
                            static_cast<uint8_t>(def->result_class()),
                        "rewritten instruction has an operand with an "
                        "incompatible result class");
                    if(operand_class == OperandClass::ProgramValue &&
                       required_representation != ValueRepresentation::None)
                    {
                        require_rewrite_invariant(
                            def->value_representation() ==
                                required_representation,
                            "rewritten instruction has an operand with an "
                            "incompatible value representation");
                    }
                });
        }

        struct StagedBlockRewrite
        {
            Block *block;
            std::vector<Instruction *> instructions;
            std::vector<Instruction *> removed_originals;
        };
    }  // namespace

    RewriteSummary GraphRewriter::rewrite_instructions_erased(
        InstructionTraversal traversal, RewriteInput input, void *callback,
        ErasedCallback invoke_callback)
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
        assert(invoke_callback != nullptr);

        GraphQueries queries = graph_->prepare_queries(traversal.queries());
        absl::flat_hash_set<const Instruction *> allocated_instructions;
        RewriteContext context(session_, arena_, &allocated_instructions);
        absl::flat_hash_set<const Instruction *> staged_instruction_set;
        std::vector<StagedBlockRewrite> staged_blocks;
        staged_blocks.reserve(graph_->blocks_.size());
        RewriteSummary summary;

        for(Block *block: graph_->blocks_)
        {
            assert(block != nullptr);
            StagedBlockRewrite staged{block, {}, {}};
            staged.instructions.reserve(block->instructions_.size());
            DefReplacements def_replacements;
            absl::flat_hash_set<const Instruction *> available_defs;
            available_defs.insert(block->parameters_.begin(),
                                  block->parameters_.end());

            for(Instruction *original: block->instructions_)
            {
                assert(original != nullptr);
                Instruction *callback_input = original;
                if(input == RewriteInput::Normalized)
                {
                    SequenceReplacements no_sequence_replacements;
                    DefResolver resolver(def_replacements,
                                         no_sequence_replacements);
                    callback_input = rebuild_instruction_with_operands(
                        *original, resolver, context);
                    validate_available_operands(*callback_input,
                                                available_defs);
                }
                RewriteResult result = invoke_callback(
                    callback, context, queries, *block, *callback_input);
                SequenceReplacements sequence_replacements;
                RewriteResult::InstructionSequence proposed_instructions;
                Instruction *proposed_replacement = nullptr;
                bool replacement_is_existing_def = false;
                bool explicitly_erased = false;

                switch(result.kind_)
                {
                    case RewriteResult::Kind::Keep:
                        proposed_instructions.push_back(callback_input);
                        proposed_replacement =
                            callback_input->result_class() == ResultClass::None
                                ? nullptr
                                : callback_input;
                        break;
                    case RewriteResult::Kind::KeepWithPrefix:
                        proposed_instructions = std::move(result.instructions_);
                        proposed_instructions.push_back(callback_input);
                        proposed_replacement =
                            callback_input->result_class() == ResultClass::None
                                ? nullptr
                                : callback_input;
                        break;
                    case RewriteResult::Kind::KeepWithSuffix:
                        proposed_instructions.push_back(callback_input);
                        proposed_instructions.insert(
                            proposed_instructions.end(),
                            result.instructions_.begin(),
                            result.instructions_.end());
                        proposed_replacement =
                            callback_input->result_class() == ResultClass::None
                                ? nullptr
                                : callback_input;
                        break;
                    case RewriteResult::Kind::Erase:
                        explicitly_erased = true;
                        break;
                    case RewriteResult::Kind::Replace:
                        proposed_instructions = std::move(result.instructions_);
                        proposed_replacement = result.replacement_def_;
                        if(original->result_class() == ResultClass::None)
                        {
                            proposed_replacement = nullptr;
                        }
                        break;
                    case RewriteResult::Kind::ReplaceWithoutResult:
                        require_rewrite_invariant(
                            original->result_class() == ResultClass::None,
                            "replace_without_result requires an instruction "
                            "without a result");
                        proposed_instructions = std::move(result.instructions_);
                        break;
                    case RewriteResult::Kind::ReplaceWithDef:
                        require_rewrite_invariant(
                            original->result_class() != ResultClass::None,
                            "replace_with_def requires a result-producing "
                            "instruction");
                        assert(result.instructions_.empty());
                        proposed_replacement = result.replacement_def_;
                        replacement_is_existing_def = true;
                        break;
                }

                size_t output_start = staged.instructions.size();
                bool keeps_callback_input =
                    result.kind_ == RewriteResult::Kind::Keep ||
                    result.kind_ == RewriteResult::Kind::KeepWithPrefix ||
                    result.kind_ == RewriteResult::Kind::KeepWithSuffix;
                for(Instruction *proposed: proposed_instructions)
                {
                    require_rewrite_invariant(
                        proposed != nullptr,
                        "JIT rewrite emitted a null instruction");
                    if(!keeps_callback_input || proposed != callback_input)
                    {
                        require_rewrite_invariant(
                            allocated_instructions.contains(proposed),
                            "replacement instructions must be allocated "
                            "through this rewrite's context");
                    }
                    require_rewrite_invariant(
                        !sequence_replacements.contains(proposed),
                        "a replacement sequence may not emit one instruction "
                        "twice");

                    DefResolver resolver(def_replacements,
                                         sequence_replacements);
                    Instruction *normalized = rebuild_instruction_with_operands(
                        *proposed, resolver, context);
                    require_rewrite_invariant(
                        !normalized->is_detached(),
                        "a detached instruction cannot be emitted");
                    require_rewrite_invariant(
                        !is_parameter_kind(normalized->kind()),
                        "block-parameter instructions cannot be emitted into a "
                        "block body");
                    require_rewrite_invariant(
                        staged_instruction_set.insert(normalized).second,
                        "an instruction cannot occupy more than one graph "
                        "position");
                    sequence_replacements.emplace(proposed, normalized);
                    validate_available_operands(*normalized, available_defs);
                    staged.instructions.push_back(normalized);
                    if(normalized->result_class() != ResultClass::None)
                    {
                        available_defs.insert(normalized);
                    }
                }

                Instruction *normalized_replacement = nullptr;
                if(proposed_replacement != nullptr)
                {
                    DefResolver resolver(def_replacements,
                                         sequence_replacements);
                    if(replacement_is_existing_def)
                    {
                        normalized_replacement =
                            resolver.resolve(proposed_replacement);
                        require_rewrite_invariant(
                            available_defs.contains(normalized_replacement),
                            "replace_with_def requires a definition already "
                            "available in the staged block");
                    }
                    else
                    {
                        auto replacement =
                            sequence_replacements.find(proposed_replacement);
                        require_rewrite_invariant(
                            replacement != sequence_replacements.end(),
                            "a replacement result must be emitted by its "
                            "replacement sequence");
                        normalized_replacement = replacement->second;
                    }
                    require_rewrite_invariant(
                        compatible_results(*original, *normalized_replacement),
                        "a replacement definition has an incompatible result "
                        "class or value representation");
                }

                if(original->result_class() != ResultClass::None)
                {
                    if(explicitly_erased)
                    {
                        def_replacements.emplace(original,
                                                 DefReplacement{nullptr, true});
                    }
                    else
                    {
                        require_rewrite_invariant(
                            normalized_replacement != nullptr,
                            "a result-producing instruction requires a "
                            "replacement definition");
                        def_replacements.emplace(
                            original,
                            DefReplacement{normalized_replacement, false});
                    }
                }
                else
                {
                    require_rewrite_invariant(
                        normalized_replacement == nullptr,
                        "an instruction without a result cannot have a "
                        "replacement definition");
                }

                size_t output_count = staged.instructions.size() - output_start;
                bool position_unchanged =
                    output_count == 1 &&
                    staged.instructions[output_start] == original;
                bool original_retained = false;
                for(size_t index = output_start;
                    index < staged.instructions.size(); ++index)
                {
                    original_retained |= staged.instructions[index] == original;
                }
                if(!original_retained)
                {
                    staged.removed_originals.push_back(original);
                }
                if(!position_unchanged)
                {
                    summary.instructions_changed = true;
                }

                bool original_is_terminator = original->is_block_terminator();
                for(size_t index = output_start;
                    index < staged.instructions.size(); ++index)
                {
                    bool is_final_output =
                        index + 1 == staged.instructions.size();
                    bool emitted_terminator =
                        staged.instructions[index]->is_block_terminator();
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
                    Instruction *new_terminator = staged.instructions.back();
                    require_rewrite_invariant(
                        new_terminator->is_block_terminator(),
                        "a terminator replacement must end in a terminator");
                    require_rewrite_invariant(
                        same_successor_edges(*original, *new_terminator),
                        "instruction rewriting cannot change CFG successor "
                        "edges");
                    summary.terminators_changed |= new_terminator != original;
                }
            }

            require_rewrite_invariant(!staged.instructions.empty(),
                                      "a rewritten block cannot be empty");
            require_rewrite_invariant(
                staged.instructions.back()->is_block_terminator(),
                "a rewritten block must end in a terminator");
            staged_blocks.push_back(std::move(staged));
        }

        if(!summary.instructions_changed)
        {
            return summary;
        }

        for(StagedBlockRewrite &staged: staged_blocks)
        {
            staged.block->instructions_.swap(staged.instructions);
        }
        for(StagedBlockRewrite &staged: staged_blocks)
        {
            for(Instruction *removed: staged.removed_originals)
            {
                removed->detach_and_poison();
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

}  // namespace cl::jit
