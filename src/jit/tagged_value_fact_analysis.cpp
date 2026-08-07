#include "jit/tagged_value_fact_analysis.h"

#include "jit/compilation_storage.h"
#include "jit/control_flow_graph.h"
#include "object_model/class_object.h"
#include "runtime/fatal.h"
#include "runtime/thread_state.h"

#include <absl/container/flat_hash_set.h>

#include <cassert>
#include <cstdint>
#include <vector>

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
        for(const Block *block: graph.blocks())
        {
            definition_count += block->parameters().size();
            definition_count += block->instructions().size();
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

        for(Instruction parameter: graph.entry_block()->parameters())
        {
            if(parameter.result_class() == ResultClass::ProgramValue &&
               parameter.value_representation() ==
                   ValueRepresentation::TaggedValue)
            {
                widen(parameter.id(), TaggedValueSet::unknown());
            }
        }

        absl::flat_hash_set<const Block *> reachable;
        absl::flat_hash_set<const Block *> queued;
        std::vector<const Block *> worklist;
        reachable.insert(graph.entry_block());
        queued.insert(graph.entry_block());
        worklist.push_back(graph.entry_block());

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

        while(!worklist.empty())
        {
            const Block *block = worklist.back();
            worklist.pop_back();
            queued.erase(block);

            for(Instruction instruction: block->instructions())
            {
                if(instruction.result_class() != ResultClass::ProgramValue ||
                   instruction.value_representation() !=
                       ValueRepresentation::TaggedValue)
                {
                    continue;
                }
                widen(instruction.id(), instruction_facts(instruction));
            }

            for(const BlockEdge *edge: block->block_successor_edges())
            {
                const Block *target = edge->target();
                bool target_changed = reachable.insert(target).second;
                assert(edge->arguments().size() == target->parameters().size());
                for(size_t index = 0; index < target->parameters().size();
                    ++index)
                {
                    Instruction parameter = target->parameter_at(index);
                    if(parameter.result_class() != ResultClass::ProgramValue ||
                       parameter.value_representation() !=
                           ValueRepresentation::TaggedValue)
                    {
                        continue;
                    }
                    target_changed |= widen(
                        parameter.id(), facts_for(edge->arguments()[index]));
                }
                if(target_changed && queued.insert(target).second)
                {
                    worklist.push_back(target);
                }
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
