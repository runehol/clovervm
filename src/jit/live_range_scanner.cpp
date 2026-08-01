#include "jit/register_allocator_internal.h"

#include "runtime/fatal.h"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>

namespace cl::jit
{
    namespace
    {
        LivenessPosition position_at(LivenessPosition start, size_t offset)
        {
            return LivenessPosition(start.value() + offset);
        }

        RegisterClass
        requirement_register_class(LocationRequirement requirement)
        {
            switch(requirement.kind())
            {
                case LocationRequirement::Kind::AnyLocation:
                    break;
                case LocationRequirement::Kind::AnyRegister:
                    return requirement.register_class();
                case LocationRequirement::Kind::FixedLocation:
                    {
                        PhysicalLocation location =
                            requirement.fixed_location();
                        if(location.is_stack())
                        {
                            fatal("JIT allocator temporary requires a register "
                                  "location");
                        }
                        return location.reg().register_class();
                    }
                case LocationRequirement::Kind::SameAsInput:
                    break;
            }
            fatal("unresolved SameAsInput in JIT allocator preparation");
        }

        bool requirement_requires_register(LocationRequirement requirement)
        {
            switch(requirement.kind())
            {
                case LocationRequirement::Kind::AnyLocation:
                    return false;
                case LocationRequirement::Kind::AnyRegister:
                    return true;
                case LocationRequirement::Kind::FixedLocation:
                    return requirement.fixed_location().is_register();
                case LocationRequirement::Kind::SameAsInput:
                    break;
            }
            fatal("unresolved SameAsInput in JIT allocator preparation");
        }

        const RegisterClassDefinition *
        find_register_class(const AllocationConstraints &constraints,
                            RegisterClass register_class)
        {
            for(const RegisterClassDefinition &definition:
                constraints.register_classes())
            {
                if(definition.register_class() == register_class)
                {
                    return &definition;
                }
            }
            return nullptr;
        }

        void validate_requirement(const AllocationConstraints &constraints,
                                  LocationRequirement requirement,
                                  RegisterClass expected_class)
        {
            if(requirement.kind() == LocationRequirement::Kind::SameAsInput)
            {
                fatal("unresolved SameAsInput in JIT allocator preparation");
            }

            const RegisterClassDefinition *definition =
                find_register_class(constraints, expected_class);
            if(definition == nullptr)
            {
                fatal("JIT allocator has no definition for a required register "
                      "class");
            }

            if(requirement.kind() == LocationRequirement::Kind::AnyRegister)
            {
                if(requirement.register_class() != expected_class)
                {
                    fatal("JIT allocator occurrence has incompatible register "
                          "classes");
                }
                return;
            }
            if(requirement.kind() == LocationRequirement::Kind::AnyLocation)
            {
                return;
            }

            PhysicalLocation location = requirement.fixed_location();
            if(location.is_register() &&
               (location.reg().register_class() != expected_class ||
                !definition->members().contains(location.reg())))
            {
                fatal("JIT allocator fixed register is incompatible with its "
                      "value");
            }
        }

        const ProgramValueUseConstraint *
        find_input_override(const InstructionAllocationConstraints *constraints,
                            uint32_t operand_index)
        {
            if(constraints == nullptr)
            {
                return nullptr;
            }
            for(const ProgramValueUseConstraint &input:
                constraints->input_overrides())
            {
                if(input.operand_index == operand_index)
                {
                    return &input;
                }
            }
            return nullptr;
        }

        class LiveRangeScanner
        {
        public:
            LiveRangeScanner(const ControlFlowGraph &graph,
                             const AllocationConstraints &constraints)
                : graph_(graph), constraints_(constraints)
            {
                for(const InstructionAllocationConstraints &override:
                    constraints_.instruction_overrides())
                {
                    override.validate(*graph_.storage());
                    auto [position, inserted] = overrides_.emplace(
                        override.instruction_id(), &override);
                    (void)position;
                    if(!inserted)
                    {
                        fatal("duplicate JIT allocation constraint override");
                    }
                }
            }

            Result<LiveRangeScan, RegisterAllocationError> scan()
            {
                linearize_blocks();
                for(size_t block_index = 0;
                    block_index < graph_.blocks().size(); ++block_index)
                {
                    auto result =
                        scan_block(*graph_.blocks()[block_index],
                                   block_ranges_[block_index].range.start);
                    if(!result)
                    {
                        return propagate_failure(std::move(result));
                    }
                }
                sort_live_range_references();
                build_block_edge_affinities();
                require_all_overrides_consumed();
                return Result<LiveRangeScan, RegisterAllocationError>::ok(
                    {std::move(block_ranges_), std::move(occurrences_),
                     std::move(fixed_constraints_), std::move(live_ranges_),
                     std::move(clobbers_), std::move(bundle_affinities_)});
            }

        private:
            OccurrenceId result_occurrence(InstructionId instruction) const
            {
                LiveRangeId range = value_ranges_.at(instruction);
                for(OccurrenceId occurrence:
                    live_ranges_[range.value()].occurrences)
                {
                    const OccurrenceAnchor &anchor =
                        occurrences_[occurrence.value()].anchor;
                    if(anchor.kind() ==
                           OccurrenceAnchor::Kind::InstructionResult &&
                       anchor.instruction_id() == instruction)
                    {
                        return occurrence;
                    }
                }
                fatal("JIT allocator result has no defining occurrence");
            }

            OccurrenceId edge_argument_occurrence(const BlockEdge *edge,
                                                  size_t argument_index) const
            {
                InstructionId definition =
                    edge->arguments()[argument_index].instruction_id();
                LiveRangeId range = value_ranges_.at(definition);
                for(OccurrenceId occurrence:
                    live_ranges_[range.value()].occurrences)
                {
                    const OccurrenceAnchor &anchor =
                        occurrences_[occurrence.value()].anchor;
                    if(anchor.kind() ==
                           OccurrenceAnchor::Kind::BlockEdgeArgument &&
                       anchor.block_edge() == edge &&
                       anchor.index() == argument_index)
                    {
                        return occurrence;
                    }
                }
                fatal("JIT allocator edge argument has no occurrence");
            }

            void build_block_edge_affinities()
            {
                for(const Block *block: graph_.blocks())
                {
                    for(const BlockEdge *edge: block->block_successor_edges())
                    {
                        assert(edge->arguments().size() ==
                               edge->target()->parameters().size());
                        for(size_t index = 0; index < edge->arguments().size();
                            ++index)
                        {
                            bundle_affinities_.push_back(
                                BundleAffinity::block_edge(
                                    edge, static_cast<uint32_t>(index),
                                    edge_argument_occurrence(edge, index),
                                    result_occurrence(edge->target()
                                                          ->parameter_at(index)
                                                          .id())));
                        }
                    }
                }
            }

            void sort_live_range_references()
            {
                for(LiveRange &live_range: live_ranges_)
                {
                    std::ranges::sort(live_range.occurrences,
                                      OccurrencePositionLess(occurrences_));
                    std::ranges::sort(
                        live_range.fixed_constraints,
                        FixedConstraintPositionLess(fixed_constraints_));
                }
            }

            void linearize_blocks()
            {
                size_t next_block_start = 0;
                block_ranges_.reserve(graph_.blocks().size());
                for(const Block *block: graph_.blocks())
                {
                    size_t block_size = (block->instructions().size() + 2) * 2;
                    size_t block_end = next_block_start + block_size;
                    block_ranges_.push_back(
                        {block,
                         {LivenessPosition(next_block_start),
                          LivenessPosition(block_end)}});
                    next_block_start = block_end;
                }
            }

            const InstructionAllocationConstraints *
            override_for(InstructionId instruction)
            {
                auto found = overrides_.find(instruction);
                if(found == overrides_.end())
                {
                    return nullptr;
                }
                consumed_overrides_.insert(instruction);
                return found->second;
            }

            void map_program_value(InstructionId definition,
                                   LiveRangeId live_range)
            {
                auto [position, inserted] =
                    value_ranges_.emplace(definition, live_range);
                (void)position;
                if(!inserted)
                {
                    fatal("duplicate JIT allocator ProgramValue definition");
                }
            }

            LiveRangeId add_live_range(LivenessRange range,
                                       LiveRangeOrigin origin,
                                       const Block &block,
                                       RegisterClass register_class)
            {
                LiveRangeId id(static_cast<uint32_t>(live_ranges_.size()));
                live_ranges_.push_back(
                    {range, std::move(origin), &block, register_class, {}, {}});
                if(live_ranges_.back().origin.kind() ==
                   LiveRangeOrigin::Kind::ProgramValue)
                {
                    map_program_value(
                        live_ranges_.back().origin.instruction_id(), id);
                }
                return id;
            }

            OccurrenceId add_occurrence(LiveRangeId live_range_id,
                                        LivenessPosition position,
                                        LivenessRange coverage,
                                        OccurrenceKind kind,
                                        OccurrenceAnchor anchor,
                                        LocationRequirement requirement)
            {
                LiveRange &live_range = live_ranges_[live_range_id.value()];
                validate_requirement(constraints_, requirement,
                                     live_range.register_class);
                if(!coverage.contains(position))
                {
                    fatal("JIT allocator occurrence lies outside its minimum "
                          "liveness coverage");
                }

                OccurrenceId occurrence_id(
                    static_cast<uint32_t>(occurrences_.size()));
                occurrences_.push_back(
                    {position, coverage, live_range_id, kind, std::move(anchor),
                     requirement_requires_register(requirement), 0});
                live_range.occurrences.push_back(occurrence_id);
                if(coverage.end > live_range.range.end)
                {
                    live_range.range.end = coverage.end;
                }

                if(requirement.kind() ==
                   LocationRequirement::Kind::FixedLocation)
                {
                    FixedConstraintId fixed_id(
                        static_cast<uint32_t>(fixed_constraints_.size()));
                    fixed_constraints_.push_back(
                        {position, requirement.fixed_location(), live_range_id,
                         occurrence_id});
                    live_range.fixed_constraints.push_back(fixed_id);
                }
                return occurrence_id;
            }

            LiveRangeId value_range(InstructionId definition,
                                    const Block &block) const
            {
                auto found = value_ranges_.find(definition);
                if(found == value_ranges_.end())
                {
                    fatal("JIT allocator use has no prepared definition");
                }
                LiveRangeId result = found->second;
                if(live_ranges_[result.value()].block != &block)
                {
                    fatal("JIT allocator encountered a direct cross-block SSA "
                          "use");
                }
                return result;
            }

            Result<void, RegisterAllocationError>
            scan_block(const Block &block, LivenessPosition block_start)
            {
                LivenessPosition entry_after = position_at(block_start, 1);
                for(Instruction parameter: block.parameters())
                {
                    const InstructionAllocationConstraints *override =
                        override_for(parameter.id());
                    if(override != nullptr &&
                       (!override->temporaries().empty() ||
                        override->clobbers().size() != 0))
                    {
                        fatal("JIT block parameters cannot have temporaries or "
                              "clobbers");
                    }
                    bool has_result_override =
                        override != nullptr &&
                        override->result_override().has_value();
                    ResultConstraint result_constraint =
                        has_result_override
                            ? *override->result_override()
                            : default_result_constraint(
                                  parameter.value_representation());
                    if(&block != graph_.entry_block() && !has_result_override)
                    {
                        result_constraint.requirement =
                            LocationRequirement::any_location();
                    }
                    if(result_constraint.requirement.kind() ==
                       LocationRequirement::Kind::SameAsInput)
                    {
                        return Result<void, RegisterAllocationError>::error(
                            RegisterAllocationError::UnsupportedSameAsInput);
                    }

                    RegisterClass register_class =
                        register_class_for_representation(
                            parameter.value_representation());
                    LiveRangeId live_range = add_live_range(
                        {entry_after, entry_after.next()},
                        LiveRangeOrigin::program_value(parameter), block,
                        register_class);
                    add_occurrence(
                        live_range, entry_after,
                        {entry_after, entry_after.next()}, OccurrenceKind::Def,
                        OccurrenceAnchor::instruction_result(parameter),
                        result_constraint.requirement);
                }

                for(size_t instruction_index = 0;
                    instruction_index < block.instructions().size();
                    ++instruction_index)
                {
                    Instruction instruction =
                        block.instruction_at(instruction_index);
                    LivenessPosition early =
                        position_at(block_start, 2 + instruction_index * 2);
                    LivenessPosition late = early.next();
                    const InstructionAllocationConstraints *override =
                        override_for(instruction.id());
                    absl::flat_hash_map<uint32_t, OccurrenceId>
                        input_occurrence_by_operand;
                    std::optional<InstructionId> operand_zero_definition;

                    if(instruction.kind() != InstructionKind::Snapshot)
                    {
                        bool has_snapshot_operand = false;
                        visit_operand_references(
                            instruction, [&](uint32_t operand_index,
                                             OperandClass operand_class,
                                             ValueRepresentationRequirement
                                                 required_representation,
                                             InstructionId definition_id) {
                                if(operand_class == OperandClass::Snapshot)
                                {
                                    has_snapshot_operand = true;
                                    return;
                                }
                                if(operand_index == 0)
                                {
                                    operand_zero_definition = definition_id;
                                }

                                ValueRepresentation representation =
                                    graph_.storage()
                                        ->instruction(definition_id)
                                        .value_representation();
                                if(!representation_matches(
                                       required_representation, representation))
                                {
                                    fatal("JIT operand has an incompatible "
                                          "value representation");
                                }
                                const ProgramValueUseConstraint *input =
                                    find_input_override(override,
                                                        operand_index);
                                ProgramValueUseConstraint constraint =
                                    input != nullptr
                                        ? *input
                                        : default_program_value_use_constraint(
                                              operand_index, representation);
                                LivenessPosition position =
                                    constraint.timing == AccessTiming::Early
                                        ? early
                                        : late;
                                LiveRangeId live_range =
                                    value_range(definition_id, block);
                                OccurrenceId occurrence = add_occurrence(
                                    live_range, position,
                                    minimum_liveness_coverage(
                                        early, OccurrenceKind::Use,
                                        constraint.timing),
                                    OccurrenceKind::Use,
                                    OccurrenceAnchor::instruction_operand(
                                        instruction, operand_index),
                                    constraint.requirement);
                                input_occurrence_by_operand.emplace(
                                    operand_index, occurrence);
                            });
                        if(has_snapshot_operand)
                        {
                            return Result<void, RegisterAllocationError>::error(
                                RegisterAllocationError::
                                    UnsupportedSnapshotConsumer);
                        }
                    }

                    if(instruction.result_class() == ResultClass::ProgramValue)
                    {
                        ResultDefinitionKind definition_kind =
                            instruction_kind_metadata(instruction.kind())
                                .result_definition_kind;
                        if(definition_kind ==
                           ResultDefinitionKind::ForwardingDef)
                        {
                            const InstructionFamilyMetadata &metadata =
                                instruction_kind_metadata(instruction.kind());
                            auto source_occurrence =
                                input_occurrence_by_operand.find(0);
                            if(metadata.fixed_operand_count == 0 ||
                               !operand_zero_definition.has_value() ||
                               source_occurrence ==
                                   input_occurrence_by_operand.end())
                            {
                                fatal("JIT forwarding definition has no fixed "
                                      "ProgramValue operand 0");
                            }
                            Instruction source = graph_.storage()->instruction(
                                *operand_zero_definition);
                            if(source.value_representation() !=
                               instruction.value_representation())
                            {
                                fatal("JIT forwarding definition changes "
                                      "value representation");
                            }

                            LiveRangeId live_range =
                                occurrences_[source_occurrence->second.value()]
                                    .live_range;
                            map_program_value(instruction.id(), live_range);
                            LivenessRange coverage = minimum_liveness_coverage(
                                early, OccurrenceKind::ForwardingDef,
                                AccessTiming::Late);
                            add_occurrence(live_range, late, coverage,
                                           OccurrenceKind::ForwardingDef,
                                           OccurrenceAnchor::instruction_result(
                                               instruction),
                                           LocationRequirement::any_register(
                                               live_ranges_[live_range.value()]
                                                   .register_class));
                        }
                        else
                        {
                            if(definition_kind != ResultDefinitionKind::Def)
                            {
                                fatal("JIT ProgramValue result has no "
                                      "definition");
                            }

                            ResultConstraint result_constraint =
                                override != nullptr &&
                                        override->result_override().has_value()
                                    ? *override->result_override()
                                    : default_result_constraint(
                                          instruction.value_representation());
                            LivenessPosition position =
                                result_constraint.timing == AccessTiming::Early
                                    ? early
                                    : late;
                            LivenessRange coverage = minimum_liveness_coverage(
                                early, OccurrenceKind::Def,
                                result_constraint.timing);
                            RegisterClass register_class =
                                register_class_for_representation(
                                    instruction.value_representation());
                            LocationRequirement result_requirement =
                                result_constraint.requirement;
                            std::optional<uint32_t> same_as_input;
                            if(result_requirement.kind() ==
                               LocationRequirement::Kind::SameAsInput)
                            {
                                same_as_input =
                                    result_requirement.input_index();
                                result_requirement =
                                    LocationRequirement::any_register(
                                        register_class);
                            }
                            LiveRangeId live_range = add_live_range(
                                coverage,
                                LiveRangeOrigin::program_value(instruction),
                                block, register_class);
                            OccurrenceId result_occurrence = add_occurrence(
                                live_range, position, coverage,
                                OccurrenceKind::Def,
                                OccurrenceAnchor::instruction_result(
                                    instruction),
                                result_requirement);
                            if(same_as_input.has_value())
                            {
                                auto input = input_occurrence_by_operand.find(
                                    *same_as_input);
                                if(input == input_occurrence_by_operand.end())
                                {
                                    fatal("SameAsInput names no scanned input "
                                          "occurrence");
                                }
                                bundle_affinities_.push_back(
                                    BundleAffinity::same_as_input(
                                        input->second, result_occurrence));
                            }
                        }
                    }

                    if(override != nullptr)
                    {
                        for(size_t temporary_index = 0;
                            temporary_index < override->temporaries().size();
                            ++temporary_index)
                        {
                            LocationRequirement requirement =
                                override->temporaries()[temporary_index]
                                    .requirement;
                            RegisterClass register_class =
                                requirement_register_class(requirement);
                            LiveRangeId live_range = add_live_range(
                                {early, late.next()},
                                LiveRangeOrigin::temporary(instruction,
                                                           temporary_index),
                                block, register_class);
                            add_occurrence(
                                live_range, early, {early, late.next()},
                                OccurrenceKind::Temporary,
                                OccurrenceAnchor::instruction_temporary(
                                    instruction, temporary_index),
                                requirement);
                        }

                        for(size_t class_index = 0;
                            class_index <
                            static_cast<size_t>(RegisterClass::Count);
                            ++class_index)
                        {
                            RegisterClass register_class =
                                static_cast<RegisterClass>(class_index);
                            for(size_t number = 0;
                                number < PhysicalRegister::MaxRegistersPerClass;
                                ++number)
                            {
                                PhysicalRegister reg(
                                    register_class,
                                    static_cast<uint8_t>(number));
                                if(override->clobbers().contains(reg))
                                {
                                    clobbers_.push_back({{late, late.next()},
                                                         reg,
                                                         instruction.id()});
                                }
                            }
                        }
                    }
                }

                LivenessPosition exit_before = position_at(
                    block_start, 2 + block.instructions().size() * 2);
                for(const BlockEdge *edge: block.block_successor_edges())
                {
                    const std::vector<ProgramValueRef> &arguments =
                        edge->arguments();
                    for(size_t argument_index = 0;
                        argument_index < arguments.size(); ++argument_index)
                    {
                        LiveRangeId live_range = value_range(
                            arguments[argument_index].instruction_id(), block);
                        add_occurrence(live_range, exit_before,
                                       {exit_before, exit_before.next()},
                                       OccurrenceKind::Use,
                                       OccurrenceAnchor::block_edge_argument(
                                           edge, argument_index),
                                       LocationRequirement::any_location());
                    }
                }
                return Result<void, RegisterAllocationError>::ok();
            }

            void require_all_overrides_consumed() const
            {
                if(consumed_overrides_.size() != overrides_.size())
                {
                    fatal("JIT allocation constraint override names an "
                          "instruction outside the graph");
                }
            }

            const ControlFlowGraph &graph_;
            const AllocationConstraints &constraints_;
            absl::flat_hash_map<InstructionId,
                                const InstructionAllocationConstraints *>
                overrides_;
            absl::flat_hash_set<InstructionId> consumed_overrides_;
            absl::flat_hash_map<InstructionId, LiveRangeId> value_ranges_;
            std::vector<BlockLivenessRange> block_ranges_;
            std::vector<Occurrence> occurrences_;
            std::vector<FixedLocationConstraint> fixed_constraints_;
            std::vector<LiveRange> live_ranges_;
            std::vector<ClobberReservation> clobbers_;
            std::vector<BundleAffinity> bundle_affinities_;
        };
    }  // namespace

    Result<LiveRangeScan, RegisterAllocationError>
    scan_live_ranges(const ControlFlowGraph &graph,
                     const AllocationConstraints &constraints)
    {
        assert(graph.is_published());
        return LiveRangeScanner(graph, constraints).scan();
    }

}  // namespace cl::jit
