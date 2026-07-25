#include "jit/register_allocator_internal.h"

#include "runtime/fatal.h"

#include <cassert>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cl::jit
{
    namespace
    {
        ProgramPoint point_at(ProgramPoint start, size_t offset)
        {
            return ProgramPoint(start.value() + offset);
        }

        RegisterClass
        requirement_register_class(LocationRequirement requirement)
        {
            switch(requirement.kind())
            {
                case LocationRequirement::Kind::AnyRegister:
                    return requirement.register_class();
                case LocationRequirement::Kind::FixedLocation:
                    {
                        AllocationLocation location =
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

            AllocationLocation location = requirement.fixed_location();
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
                    override.validate();
                    auto [position, inserted] =
                        overrides_.emplace(override.instruction(), &override);
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
                require_all_overrides_consumed();
                return Result<LiveRangeScan, RegisterAllocationError>::ok(
                    {std::move(block_ranges_), std::move(occurrences_),
                     std::move(fixed_constraints_), std::move(live_ranges_),
                     std::move(clobbers_)});
            }

        private:
            void linearize_blocks()
            {
                size_t next_block_start = 0;
                block_ranges_.reserve(graph_.blocks().size());
                for(const Block *block: graph_.blocks())
                {
                    size_t block_size = (block->instructions().size() + 2) * 2;
                    size_t block_end = next_block_start + block_size;
                    block_ranges_.push_back({block,
                                             {ProgramPoint(next_block_start),
                                              ProgramPoint(block_end)}});
                    next_block_start = block_end;
                }
            }

            const InstructionAllocationConstraints *
            override_for(const Instruction *instruction)
            {
                auto found = overrides_.find(instruction);
                if(found == overrides_.end())
                {
                    return nullptr;
                }
                consumed_overrides_.insert(instruction);
                return found->second;
            }

            LiveRangeId add_live_range(ProgramRange range,
                                       LiveRangeOrigin origin,
                                       const Block &block,
                                       RegisterClass register_class)
            {
                LiveRangeId id(live_ranges_.size());
                live_ranges_.push_back(
                    {range, std::move(origin), &block, register_class, {}, {}});
                if(live_ranges_.back().origin.kind() ==
                   LiveRangeOrigin::Kind::ProgramValue)
                {
                    auto [position, inserted] = value_ranges_.emplace(
                        live_ranges_.back().origin.instruction(), id);
                    (void)position;
                    if(!inserted)
                    {
                        fatal("duplicate JIT allocator ProgramValue origin");
                    }
                }
                return id;
            }

            OccurrenceId add_occurrence(LiveRangeId live_range_id,
                                        ProgramPoint point, OccurrenceKind kind,
                                        OccurrenceAnchor anchor,
                                        LocationRequirement requirement)
            {
                LiveRange &live_range = live_ranges_[live_range_id.value()];
                validate_requirement(constraints_, requirement,
                                     live_range.register_class);

                OccurrenceId occurrence_id(occurrences_.size());
                occurrences_.push_back(
                    {point, live_range_id, kind, std::move(anchor), 0});
                live_range.occurrences.push_back(occurrence_id);
                if(point.next() > live_range.range.end)
                {
                    live_range.range.end = point.next();
                }

                if(requirement.kind() ==
                   LocationRequirement::Kind::FixedLocation)
                {
                    FixedConstraintId fixed_id(fixed_constraints_.size());
                    fixed_constraints_.push_back(
                        {point, requirement.fixed_location(), live_range_id,
                         occurrence_id});
                    live_range.fixed_constraints.push_back(fixed_id);
                }
                return occurrence_id;
            }

            LiveRangeId value_range(const Instruction *definition,
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
            scan_block(const Block &block, ProgramPoint block_start)
            {
                ProgramPoint entry_after = point_at(block_start, 1);
                for(Instruction *parameter: block.parameters())
                {
                    const InstructionAllocationConstraints *override =
                        override_for(parameter);
                    if(override != nullptr &&
                       (!override->temporaries().empty() ||
                        override->clobbers().size() != 0))
                    {
                        fatal("JIT block parameters cannot have temporaries or "
                              "clobbers");
                    }
                    ResultConstraint result_constraint =
                        override != nullptr &&
                                override->result_override().has_value()
                            ? *override->result_override()
                            : default_result_constraint(
                                  parameter->value_representation());
                    if(result_constraint.requirement.kind() ==
                       LocationRequirement::Kind::SameAsInput)
                    {
                        return Result<void, RegisterAllocationError>::error(
                            RegisterAllocationError::UnsupportedSameAsInput);
                    }

                    RegisterClass register_class =
                        register_class_for_representation(
                            parameter->value_representation());
                    LiveRangeId live_range = add_live_range(
                        {entry_after, entry_after.next()},
                        LiveRangeOrigin::program_value(parameter), block,
                        register_class);
                    add_occurrence(
                        live_range, entry_after, OccurrenceKind::Def,
                        OccurrenceAnchor::instruction_result(parameter),
                        result_constraint.requirement);
                }

                for(size_t instruction_index = 0;
                    instruction_index < block.instructions().size();
                    ++instruction_index)
                {
                    Instruction *instruction =
                        block.instructions()[instruction_index];
                    ProgramPoint early =
                        point_at(block_start, 2 + instruction_index * 2);
                    ProgramPoint late = early.next();
                    const InstructionAllocationConstraints *override =
                        override_for(instruction);

                    if(instruction->kind() != InstructionKind::Snapshot)
                    {
                        bool has_snapshot_operand = false;
                        visit_operand_references(
                            *instruction,
                            [&](uint32_t operand_index,
                                OperandClass operand_class,
                                ValueRepresentation representation,
                                Instruction *definition) {
                                if(operand_class == OperandClass::Snapshot)
                                {
                                    has_snapshot_operand = true;
                                    return;
                                }

                                const ProgramValueUseConstraint *input =
                                    find_input_override(override,
                                                        operand_index);
                                ProgramValueUseConstraint constraint =
                                    input != nullptr
                                        ? *input
                                        : default_program_value_use_constraint(
                                              operand_index, representation);
                                ProgramPoint point =
                                    constraint.timing == AccessTiming::Early
                                        ? early
                                        : late;
                                LiveRangeId live_range =
                                    value_range(definition, block);
                                add_occurrence(
                                    live_range, point, OccurrenceKind::Use,
                                    OccurrenceAnchor::instruction_operand(
                                        instruction, operand_index),
                                    constraint.requirement);
                            });
                        if(has_snapshot_operand)
                        {
                            return Result<void, RegisterAllocationError>::error(
                                RegisterAllocationError::
                                    UnsupportedSnapshotConsumer);
                        }
                    }

                    if(instruction->result_class() == ResultClass::ProgramValue)
                    {
                        ResultConstraint result_constraint =
                            override != nullptr &&
                                    override->result_override().has_value()
                                ? *override->result_override()
                                : default_result_constraint(
                                      instruction->value_representation());
                        if(result_constraint.requirement.kind() ==
                           LocationRequirement::Kind::SameAsInput)
                        {
                            return Result<void, RegisterAllocationError>::error(
                                RegisterAllocationError::
                                    UnsupportedSameAsInput);
                        }
                        ProgramPoint point =
                            result_constraint.timing == AccessTiming::Early
                                ? early
                                : late;
                        RegisterClass register_class =
                            register_class_for_representation(
                                instruction->value_representation());
                        LiveRangeId live_range = add_live_range(
                            {point, point.next()},
                            LiveRangeOrigin::program_value(instruction), block,
                            register_class);
                        add_occurrence(
                            live_range, point, OccurrenceKind::Def,
                            OccurrenceAnchor::instruction_result(instruction),
                            result_constraint.requirement);
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
                                live_range, early, OccurrenceKind::Temporary,
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
                                                         instruction});
                                }
                            }
                        }
                    }
                }

                ProgramPoint exit_before =
                    point_at(block_start, 2 + block.instructions().size() * 2);
                for(const BlockEdge *edge: block.block_successor_edges())
                {
                    const std::vector<ProgramValueRef> &arguments =
                        edge->arguments();
                    for(size_t argument_index = 0;
                        argument_index < arguments.size(); ++argument_index)
                    {
                        Instruction *definition =
                            arguments[argument_index].instruction();
                        LiveRangeId live_range = value_range(definition, block);
                        LocationRequirement requirement =
                            LocationRequirement::any_register(
                                live_ranges_[live_range.value()]
                                    .register_class);
                        add_occurrence(live_range, exit_before,
                                       OccurrenceKind::Use,
                                       OccurrenceAnchor::block_edge_argument(
                                           edge, argument_index),
                                       requirement);
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
            std::unordered_map<const Instruction *,
                               const InstructionAllocationConstraints *>
                overrides_;
            std::unordered_set<const Instruction *> consumed_overrides_;
            std::unordered_map<const Instruction *, LiveRangeId> value_ranges_;
            std::vector<BlockProgramRange> block_ranges_;
            std::vector<Occurrence> occurrences_;
            std::vector<FixedLocationConstraint> fixed_constraints_;
            std::vector<LiveRange> live_ranges_;
            std::vector<ClobberReservation> clobbers_;
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
