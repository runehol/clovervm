#include "jit/constant_folding.h"

#include "builtin_types/float.h"
#include "jit/block_fixed_point.h"
#include "jit/block_parameter_join.h"
#include "jit/compilation_session.h"
#include "jit/control_flow_graph.h"
#include "jit/graph_rewriter.h"
#include "object_model/class_object.h"
#include "runtime/thread_state.h"

#include <absl/container/flat_hash_map.h>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <variant>

namespace cl::jit
{
    namespace
    {
        struct TaggedConstant
        {
            Value value;
        };

        struct F64Constant
        {
            double value;

            uint64_t bits() const { return std::bit_cast<uint64_t>(value); }
        };

        using ConstantValue = std::variant<TaggedConstant, F64Constant>;

        bool same_constant(const ConstantValue &lhs, const ConstantValue &rhs)
        {
            if(lhs.index() != rhs.index())
            {
                return false;
            }
            if(const TaggedConstant *tagged = std::get_if<TaggedConstant>(&lhs))
            {
                return tagged->value == std::get<TaggedConstant>(rhs).value;
            }
            return std::get<F64Constant>(lhs).bits() ==
                   std::get<F64Constant>(rhs).bits();
        }

        std::optional<ConstantValue>
        constant_value(const Instruction &instruction)
        {
            switch(instruction.kind())
            {
                case InstructionKind::Const:
                    return TaggedConstant{
                        instruction.as<ConstInstruction>().constant()};
                case InstructionKind::ConstF64:
                    return F64Constant{std::bit_cast<double>(
                        instruction.as<ConstF64Instruction>().bits())};
                default:
                    return std::nullopt;
            }
        }

        class ConstantFact
        {
        public:
            static ConstantFact unresolved()
            {
                return ConstantFact(Kind::Unresolved);
            }

            static ConstantFact exact(ConstantValue value)
            {
                return ConstantFact(std::move(value));
            }

            static ConstantFact not_constant()
            {
                return ConstantFact(Kind::NotConstant);
            }

            bool has_constant() const { return kind_ == Kind::ExactConstant; }

            const ConstantValue &constant() const
            {
                assert(has_constant());
                return *constant_;
            }

            ConstantFact merge(const ConstantFact &other) const
            {
                if(kind_ == Kind::NotConstant ||
                   other.kind_ == Kind::NotConstant)
                {
                    return not_constant();
                }
                if(kind_ == Kind::Unresolved)
                {
                    return other;
                }
                if(other.kind_ == Kind::Unresolved)
                {
                    return *this;
                }
                return same_constant(*constant_, *other.constant_)
                           ? *this
                           : not_constant();
            }

            friend bool operator==(const ConstantFact &lhs,
                                   const ConstantFact &rhs)
            {
                if(lhs.kind_ != rhs.kind_)
                {
                    return false;
                }
                return lhs.kind_ != Kind::ExactConstant ||
                       same_constant(*lhs.constant_, *rhs.constant_);
            }

        private:
            enum class Kind : uint8_t
            {
                Unresolved,
                ExactConstant,
                NotConstant,
            };

            explicit ConstantFact(Kind kind) : kind_(kind) {}

            explicit ConstantFact(ConstantValue value)
                : kind_(Kind::ExactConstant), constant_(std::move(value))
            {
            }

            Kind kind_;
            std::optional<ConstantValue> constant_;
        };

        bool is_foldable_parameter(const Instruction &parameter)
        {
            return parameter.result_class() == ResultClass::ProgramValue &&
                   (parameter.value_representation() ==
                        ValueRepresentation::TaggedValue ||
                    parameter.value_representation() ==
                        ValueRepresentation::F64);
        }

        size_t constant_fact_revisit_budget(size_t parameter_count,
                                            size_t block_edge_count)
        {
            constexpr size_t revisits_per_graph_element = 4;
            constexpr size_t maximum = std::numeric_limits<size_t>::max();
            size_t graph_elements = parameter_count > maximum - block_edge_count
                                        ? maximum
                                        : parameter_count + block_edge_count;
            return graph_elements > maximum / revisits_per_graph_element
                       ? maximum
                       : graph_elements * revisits_per_graph_element;
        }

        class ConstantJoinAnalysis
        {
        public:
            explicit ConstantJoinAnalysis(const ControlFlowGraph &graph)
                : graph_(&graph)
            {
                size_t parameter_count = 0;
                size_t block_edge_count = 0;
                for(const Block *block: graph.blocks())
                {
                    parameter_count += block->parameters().size();
                    block_edge_count += block->block_successor_edges().size();
                    for(Instruction parameter: block->parameters())
                    {
                        if(is_foldable_parameter(parameter))
                        {
                            facts_.emplace(parameter.id(),
                                           ConstantFact::unresolved());
                        }
                    }
                }

                for(const Block *entry: graph.entry_blocks())
                {
                    for(Instruction parameter: entry->parameters())
                    {
                        auto fact = facts_.find(parameter.id());
                        if(fact != facts_.end())
                        {
                            fact->second = ConstantFact::not_constant();
                        }
                    }
                }

                FixedPointStatus status = iterate_blocks_to_fixed_point(
                    graph, BlockOrder::Forward,
                    constant_fact_revisit_budget(parameter_count,
                                                 block_edge_count),
                    [&](const Block &block) {
                        bool changed = false;
                        for(BlockParameterJoin join:
                            graph.block_parameter_joins(block))
                        {
                            auto fact = facts_.find(join.parameter().id());
                            if(fact == facts_.end())
                            {
                                continue;
                            }

                            ConstantFact incoming = ConstantFact::unresolved();
                            for(IncomingArgument argument:
                                join.incoming_arguments())
                            {
                                incoming =
                                    incoming.merge(fact_for(argument.value));
                            }
                            ConstantFact widened = fact->second.merge(incoming);
                            if(widened != fact->second)
                            {
                                fact->second = std::move(widened);
                                changed = true;
                            }
                        }
                        return changed ? DataflowUpdate::Changed
                                       : DataflowUpdate::Unchanged;
                    });

                if(status == FixedPointStatus::RevisitLimitReached)
                {
                    for(auto &fact: facts_)
                    {
                        fact.second = ConstantFact::not_constant();
                    }
                }
            }

            std::optional<ConstantValue>
            constant_for(const Instruction &parameter) const
            {
                auto fact = facts_.find(parameter.id());
                if(fact == facts_.end() || !fact->second.has_constant())
                {
                    return std::nullopt;
                }
                return fact->second.constant();
            }

        private:
            ConstantFact fact_for(ProgramValueRef value) const
            {
                Instruction definition =
                    graph_->storage()->instruction(value.instruction_id());
                if(std::optional<ConstantValue> constant =
                       constant_value(definition))
                {
                    return ConstantFact::exact(std::move(*constant));
                }
                auto fact = facts_.find(definition.id());
                return fact == facts_.end() ? ConstantFact::not_constant()
                                            : fact->second;
            }

            const ControlFlowGraph *graph_;
            absl::flat_hash_map<InstructionId, ConstantFact> facts_;
        };

        RewriteResult fold_instruction(RewriteContext &context,
                                       Shape *exact_float_shape,
                                       const Instruction &instruction)
        {
            // clang-format off
            CL_JIT_CORE_INSTRUCTION_SWITCH(instruction)
            {
                CL_JIT_CORE_INSTRUCTION_CASE(UnboxF64, unbox)
                {
                    Instruction source = context.instruction(
                        unbox.source().instruction_id());
                    if(source.kind() != InstructionKind::Const)
                    {
                        return RewriteResult::keep();
                    }
                    Value value = source.as<ConstInstruction>().constant();
                    if(!value.is_ptr() ||
                       value.get_ptr<Object>()->get_shape() != exact_float_shape)
                    {
                        return RewriteResult::keep();
                    }
                    double floating = assume_convert_to<Float>(value)->value();
                    ConstF64Instruction replacement =
                        context.make_instruction<ConstF64Instruction>(
                            std::bit_cast<uint64_t>(floating));
                    return RewriteResult::replace(replacement);
                }
                CL_JIT_CORE_INSTRUCTION_FAMILY_CASE(UnaryArithmeticF64,
                                                    arithmetic)
                {
                    switch(arithmetic.subkind())
                    {
                        case UnaryArithmeticF64Subkind::NegF64:
                            Instruction source = context.instruction(
                                arithmetic.source().instruction_id());
                            if(source.kind() != InstructionKind::ConstF64)
                            {
                                return RewriteResult::keep();
                            }
                            F64Constant operand{std::bit_cast<double>(
                                source.as<ConstF64Instruction>().bits())};
                            F64Constant result{-operand.value};
                            ConstF64Instruction replacement =
                                context.make_instruction<ConstF64Instruction>(
                                    result.bits());
                            return RewriteResult::replace(replacement);
                    }
                    return RewriteResult::keep();
                }
                default:
                    return RewriteResult::keep();
            }
            // clang-format on
        }

        class ConstantJoinRewrite
        {
        public:
            ConstantJoinRewrite(const ConstantJoinAnalysis &analysis,
                                Shape *exact_float_shape)
                : analysis_(&analysis), exact_float_shape_(exact_float_shape)
            {
            }

            BlockParameterRewrite
            block_parameter(RewriteContext &context, const GraphQueries &,
                            const BlockParameterJoin &join)
            {
                std::optional<ConstantValue> constant =
                    analysis_->constant_for(join.parameter());
                if(!constant)
                {
                    return BlockParameterRewrite::keep();
                }

                if(const TaggedConstant *tagged =
                       std::get_if<TaggedConstant>(&*constant))
                {
                    Value retained =
                        context.retain_and_pin_value(tagged->value);
                    ConstInstruction replacement =
                        context.make_instruction<ConstInstruction>(retained);
                    return BlockParameterRewrite::materialize_in_destination(
                        RewriteInsertion::insert({replacement}),
                        ProgramValueRef(replacement));
                }

                ConstF64Instruction replacement =
                    context.make_instruction<ConstF64Instruction>(
                        std::get<F64Constant>(*constant).bits());
                return BlockParameterRewrite::materialize_in_destination(
                    RewriteInsertion::insert({replacement}),
                    ProgramValueRef(replacement));
            }

            RewriteResult rewrite_instruction(RewriteContext &context,
                                              const GraphQueries &,
                                              const Block &,
                                              const Instruction &instruction)
            {
                return fold_instruction(context, exact_float_shape_,
                                        instruction);
            }

        private:
            const ConstantJoinAnalysis *analysis_;
            Shape *exact_float_shape_;
        };
    }  // namespace

    Result<bool, JitCompilationError>
    fold_constants(CompilationSession &session, ControlFlowGraph &graph)
    {
        assert(graph.ir_level() == IRLevel::Core);
        Shape *exact_float_shape =
            graph.thread_state()
                .class_for_native_layout(NativeLayoutId::Float)
                ->get_instance_root_shape();

        GraphRewriter instruction_rewriter(session, graph);
        RewriteSummary instruction_summary =
            instruction_rewriter.rewrite_instructions(
                InstructionTraversal(), RewriteInput::Normalized,
                [&](RewriteContext &context, const GraphQueries &,
                    const Block &, const Instruction &instruction) {
                    return fold_instruction(context, exact_float_shape,
                                            instruction);
                });

        ConstantJoinAnalysis analysis(graph);
        ConstantJoinRewrite rewrite(analysis, exact_float_shape);
        GraphRewriter join_rewriter(session, graph);
        RewriteSummary join_summary = join_rewriter.rewrite_instructions(
            InstructionTraversal(), RewriteInput::Normalized, rewrite);

        return Result<bool, JitCompilationError>::ok(
            instruction_summary.instructions_changed ||
            join_summary.block_parameters_changed ||
            join_summary.instructions_changed);
    }

}  // namespace cl::jit
