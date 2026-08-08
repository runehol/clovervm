#include "jit/tagged_value_fact_analysis.h"

#include "jit/block_fixed_point.h"
#include "jit/block_parameter_join.h"
#include "jit/compilation_storage.h"
#include "jit/control_flow_graph.h"
#include "object_model/class_object.h"
#include "runtime/fatal.h"
#include "runtime/thread_state.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cl::jit
{
    namespace
    {
        TaggedValueSet exact_value_facts(Value value)
        {
            if(value.is_ptr())
            {
                return TaggedValueSet::pointer();
            }
            return TaggedValueSet::from_inline_tag(
                uint8_t(value.as.integer & value_tag_mask));
        }

        size_t tagged_fact_revisit_budget(size_t definition_count,
                                          size_t block_edge_count)
        {
            constexpr size_t revisits_per_graph_element = 64;
            constexpr size_t maximum = std::numeric_limits<size_t>::max();
            size_t graph_elements =
                definition_count > maximum - block_edge_count
                    ? maximum
                    : definition_count + block_edge_count;
            return graph_elements > maximum / revisits_per_graph_element
                       ? maximum
                       : graph_elements * revisits_per_graph_element;
        }
    }  // namespace

    TaggedValueFactAnalysis::TaggedValueFactAnalysis(
        const ControlFlowGraph &graph)
        : graph_generation_(graph.mutation_generation())
    {
        assert(graph.is_published());
        Shape *float_shape = graph.thread_state()
                                 .class_for_native_layout(NativeLayoutId::Float)
                                 ->get_instance_root_shape();

        size_t definition_count = 0;
        size_t block_edge_count = 0;
        for(const Block *block: graph.blocks())
        {
            definition_count += block->parameters().size();
            definition_count += block->instructions().size();
            block_edge_count += block->block_successor_edges().size();
        }
        facts_.reserve(definition_count);

        auto add_tagged_definition = [&](Instruction instruction) {
            if(instruction.result_class() == ResultClass::ProgramValue &&
               instruction.value_representation() ==
                   ValueRepresentation::TaggedValue)
            {
                facts_.emplace(instruction.id(), TaggedValueSet::never());
            }
        };
        for(const Block *block: graph.blocks())
        {
            for(Instruction parameter: block->parameters())
            {
                add_tagged_definition(parameter);
            }
            for(Instruction instruction: block->instructions())
            {
                add_tagged_definition(instruction);
            }
        }

        auto facts_for = [&](ProgramValueRef value) -> TaggedValueSet {
            auto found = facts_.find(value.instruction_id());
            if(found == facts_.end())
            {
                fatal("JIT tagged-value facts queried for a non-tagged value");
            }
            return found->second;
        };
        auto widen = [&](InstructionId id, TaggedValueSet incoming) {
            auto found = facts_.find(id);
            assert(found != facts_.end());
            TaggedValueSet merged = found->second.merge(incoming);
            if(merged == found->second)
            {
                return false;
            }
            found->second = merged;
            return true;
        };

        for(const Block *entry: graph.entry_blocks())
        {
            for(Instruction parameter: entry->parameters())
            {
                if(parameter.result_class() == ResultClass::ProgramValue &&
                   parameter.value_representation() ==
                       ValueRepresentation::TaggedValue)
                {
                    widen(parameter.id(), TaggedValueSet::unknown());
                }
            }
        }

        auto instruction_facts = [&](Instruction instruction) {
            switch(instruction_family_kind(instruction.kind()))
            {
                case InstructionFamilyKind::Mov:
                    return facts_for(ProgramValueRef(
                        instruction.as<MovInstruction>().source()));
                case InstructionFamilyKind::LoadStack:
                    return facts_for(ProgramValueRef(
                        instruction.as<LoadStackInstruction>().source()));
                case InstructionFamilyKind::StoreStack:
                    return facts_for(ProgramValueRef(
                        instruction.as<StoreStackInstruction>().source()));
                case InstructionFamilyKind::BinaryArithmeticSMIWithSnapshot:
                case InstructionFamilyKind::BinaryLogicalSMI:
                    return TaggedValueSet::smi();
                case InstructionFamilyKind::BinaryComparisonF64:
                case InstructionFamilyKind::BinaryComparisonSMI:
                case InstructionFamilyKind::IsComparison:
                    return TaggedValueSet::boolean();
                case InstructionFamilyKind::BoxF64:
                    return TaggedValueSet::exact_shape(float_shape);
                case InstructionFamilyKind::InlineTagGuard:
                    {
                        InlineTagGuardInstruction guard =
                            instruction.as<InlineTagGuardInstruction>();
                        return facts_for(ProgramValueRef(guard.value()))
                            .intersect(TaggedValueSet::from_class(
                                guard.expected_class()));
                    }
                case InstructionFamilyKind::ShapeGuard:
                    {
                        ShapeGuardInstruction guard =
                            instruction.as<ShapeGuardInstruction>();
                        Shape *expected_shape = guard.expected_shape();
                        TaggedValueSet accepted =
                            expected_shape->has_flag(ShapeFlag::IsImmutable)
                                ? TaggedValueSet::exact_shape(expected_shape)
                                : TaggedValueSet::pointer();
                        return facts_for(ProgramValueRef(guard.object()))
                            .intersect(accepted);
                    }
                case InstructionFamilyKind::Const:
                    return exact_value_facts(
                        instruction.as<ConstInstruction>().constant());
                default:
                    break;
            }

            const InstructionFamilyMetadata &metadata =
                instruction_kind_metadata(instruction.kind());
            if(metadata.result_definition_kind ==
               ResultDefinitionKind::ForwardingDef)
            {
                assert(metadata.fixed_operand_count != 0);
                Instruction source = graph.storage()->instruction(
                    InstructionId(instruction.operand_word(0)));
                return facts_for(ProgramValueRef(source));
            }
            return TaggedValueSet::unknown();
        };

        FixedPointStatus status = iterate_blocks_to_fixed_point(
            graph, BlockOrder::Forward,
            tagged_fact_revisit_budget(facts_.size(), block_edge_count),
            [&](const Block &block) {
                bool changed = false;
                for(BlockParameterJoin join: graph.block_parameter_joins(block))
                {
                    Instruction parameter = join.parameter();
                    if(parameter.result_class() != ResultClass::ProgramValue ||
                       parameter.value_representation() !=
                           ValueRepresentation::TaggedValue)
                    {
                        continue;
                    }

                    TaggedValueSet incoming = TaggedValueSet::never();
                    for(IncomingArgument argument: join.incoming_arguments())
                    {
                        incoming = incoming.merge(facts_for(argument.value));
                    }
                    changed |= widen(parameter.id(), incoming);
                }

                for(Instruction instruction: block.instructions())
                {
                    if(instruction.result_class() !=
                           ResultClass::ProgramValue ||
                       instruction.value_representation() !=
                           ValueRepresentation::TaggedValue)
                    {
                        continue;
                    }
                    changed |=
                        widen(instruction.id(), instruction_facts(instruction));
                }
                return changed ? DataflowUpdate::Changed
                               : DataflowUpdate::Unchanged;
            });

        if(status == FixedPointStatus::RevisitLimitReached)
        {
            for(auto &fact: facts_)
            {
                fact.second = TaggedValueSet::unknown();
            }
        }
    }

    const TaggedValueSet &
    TaggedValueFactAnalysis::facts_of(ProgramValueRef value) const
    {
        auto found = facts_.find(value.instruction_id());
        if(found == facts_.end())
        {
            fatal("JIT tagged-value facts queried for a non-tagged value");
        }
        return found->second;
    }

}  // namespace cl::jit
