#include "jit/register_allocator_internal.h"

#include "runtime/fatal.h"

#include <absl/container/btree_map.h>
#include <absl/container/flat_hash_set.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <iterator>
#include <optional>
#include <queue>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        template <typename T> class PerPhysicalRegister
        {
        public:
            T &operator[](PhysicalRegister reg)
            {
                return values_[class_index(reg.register_class())][reg.number()];
            }

            const T &operator[](PhysicalRegister reg) const
            {
                return values_[class_index(reg.register_class())][reg.number()];
            }

        private:
            static constexpr size_t class_index(RegisterClass register_class)
            {
                return static_cast<size_t>(register_class);
            }

            std::array<std::array<T, PhysicalRegister::MaxRegistersPerClass>,
                       static_cast<size_t>(RegisterClass::Count)>
                values_{};
        };

        struct AssignedFragment
        {
            LivenessPosition end;
            BundleId bundle;
        };

        using RegisterOccupancy =
            absl::btree_map<LivenessPosition, AssignedFragment>;

        struct BundleWorkItem
        {
            BundleId bundle;
            size_t priority;
        };

        struct RegisterProbe
        {
            std::vector<BundleId> conflicting_bundles;
            std::optional<LivenessPosition> first_conflict;
            bool conflicts_with_clobber = false;

            void record_conflict(LivenessPosition position)
            {
                if(!first_conflict.has_value() || position < *first_conflict)
                {
                    first_conflict = position;
                }
            }

            bool fits() const
            {
                return conflicting_bundles.empty() && !conflicts_with_clobber;
            }
        };

        struct PressureSplitCandidate
        {
            LivenessPosition boundary;
            uint64_t conflict_cost;
        };

        struct SpillBoundary
        {
            LivenessRange range;
            InstructionId instruction;
        };

        struct BundleAssignmentResult
        {
            BundleLocationAssignments locations;
            uint32_t spill_slot_count;
        };

        struct ActiveSpillSlot
        {
            LivenessPosition end;
            uint32_t slot;
        };

        struct ActiveSpillSlotCompare
        {
            bool operator()(ActiveSpillSlot lhs, ActiveSpillSlot rhs) const
            {
                if(lhs.end != rhs.end)
                {
                    return lhs.end > rhs.end;
                }
                return lhs.slot > rhs.slot;
            }
        };

        class BundleWorkItemCompare
        {
        public:
            bool operator()(BundleWorkItem lhs, BundleWorkItem rhs) const
            {
                if(lhs.priority != rhs.priority)
                {
                    return lhs.priority < rhs.priority;
                }
                return lhs.bundle.value() > rhs.bundle.value();
            }
        };

        void coalesce_ranges(std::vector<LivenessRange> &ranges)
        {
            std::ranges::sort(ranges, [](LivenessRange lhs, LivenessRange rhs) {
                return lhs.start < rhs.start;
            });

            size_t output = 0;
            for(LivenessRange range: ranges)
            {
                if(output != 0 && range.start <= ranges[output - 1].end)
                {
                    ranges[output - 1].end =
                        std::max(ranges[output - 1].end, range.end);
                }
                else
                {
                    ranges[output++] = range;
                }
            }
            ranges.erase(ranges.begin() + static_cast<ptrdiff_t>(output),
                         ranges.end());
        }

        bool ranges_overlap(const RegisterOccupancy &occupancy,
                            LivenessRange candidate)
        {
            auto position = occupancy.lower_bound(candidate.start);
            if(position != occupancy.begin() &&
               std::prev(position)->second.end > candidate.start)
            {
                return true;
            }
            return position != occupancy.end() &&
                   position->first < candidate.end;
        }

        class BundleAssigner
        {
        public:
            BundleAssigner(const PreparedAllocationProblem &problem,
                           std::vector<LiveBundle> &bundles,
                           BundleTransferSchedule &transfers,
                           const AllocationConstraints &constraints)
                : problem_(problem), bundles_(bundles), transfers_(transfers),
                  constraints_(constraints),
                  location_by_bundle_(bundles.size()),
                  spill_candidate_by_bundle_(bundles.size(), false),
                  fixed_operand_copy_instruction_(problem.occurrences().size())
            {
                for(const FixedOperandCopyConstraint &copy:
                    problem_.fixed_operand_copies())
                {
                    fixed_operand_copy_instruction_[copy.source.value()] =
                        problem_.occurrences()[copy.source.value()]
                            .anchor.instruction_id();
                }
                absl::flat_hash_set<InstructionId>
                    permitted_spill_boundary_instructions;
                for(const InstructionAllocationConstraints &constraint:
                    constraints_.instruction_overrides())
                {
                    if(constraint.call_local_spill_policy() ==
                       CallLocalSpillPolicy::Allow)
                    {
                        permitted_spill_boundary_instructions.insert(
                            constraint.instruction_id());
                    }
                }
                absl::flat_hash_set<InstructionId> added_spill_boundaries;
                for(const ClobberReservation &clobber: problem_.clobbers())
                {
                    clobber_ranges_[clobber.reg].push_back(clobber.range);
                    if(permitted_spill_boundary_instructions.contains(
                           clobber.instruction) &&
                       added_spill_boundaries.insert(clobber.instruction)
                           .second)
                    {
                        assert(clobber.range.start.value() != 0);
                        spill_boundaries_.push_back(
                            {{LivenessPosition(clobber.range.start.value() - 1),
                              clobber.range.end},
                             clobber.instruction});
                    }
                }
                std::ranges::sort(spill_boundaries_, {},
                                  [](const SpillBoundary &boundary) {
                                      return boundary.range.start;
                                  });
                for(size_t class_index = 0;
                    class_index < static_cast<size_t>(RegisterClass::Count);
                    ++class_index)
                {
                    RegisterClass register_class =
                        static_cast<RegisterClass>(class_index);
                    for(size_t number = 0;
                        number < PhysicalRegister::MaxRegistersPerClass;
                        ++number)
                    {
                        PhysicalRegister reg(register_class,
                                             static_cast<uint8_t>(number));
                        coalesce_ranges(clobber_ranges_[reg]);
                    }
                }
            }

            Result<BundleAssignmentResult, RegisterAllocationError> run()
            {
                enqueue_bundles();
                while(!worklist_.empty())
                {
                    BundleId bundle_id = worklist_.top().bundle;
                    worklist_.pop();
                    const LiveBundle &bundle = bundles_[bundle_id.value()];

                    if(spill_candidate_by_bundle_[bundle_id.value()])
                    {
                        std::optional<PhysicalRegister> selected;
                        for(PhysicalRegister candidate:
                            register_class(bundle.register_class)
                                .allocation_order())
                        {
                            if(probe_register(candidate, bundle).fits())
                            {
                                selected = candidate;
                                break;
                            }
                        }
                        if(selected.has_value())
                        {
                            place(bundle_id, PhysicalLocation::reg(*selected));
                        }
                        else
                        {
                            defer_spill_slot_assignment(bundle_id);
                        }
                        continue;
                    }

                    std::optional<PhysicalLocation> selected;
                    std::optional<PhysicalLocation> required =
                        required_location(bundle);
                    if(required.has_value() && required->is_stack())
                    {
                        if(fits(required->stack(), bundle))
                        {
                            selected = required;
                        }
                    }
                    else if(required.has_value())
                    {
                        RegisterProbe probe =
                            probe_register(required->reg(), bundle);
                        if(probe.fits())
                        {
                            selected = required;
                        }
                        else if(trim_spill_carrier(bundle_id))
                        {
                            continue;
                        }
                        else if(split_before_later_fixed_use(bundle_id, probe))
                        {
                            continue;
                        }
                        else if(std::optional<uint64_t> cost =
                                    eviction_cost(probe);
                                cost.has_value() && *cost < bundle.spill_weight)
                        {
                            evict(probe);
                            selected = required;
                        }
                        else if(split_for_pressure(bundle_id, probe))
                        {
                            continue;
                        }
                    }
                    else
                    {
                        std::optional<uint64_t> lowest_eviction_cost;
                        std::optional<PhysicalRegister> evictable;
                        std::optional<RegisterProbe> eviction_probe;
                        std::optional<PressureSplitCandidate> split_candidate;
                        for(PhysicalRegister candidate:
                            register_class(bundle.register_class)
                                .allocation_order())
                        {
                            RegisterProbe probe =
                                probe_register(candidate, bundle);
                            if(probe.fits())
                            {
                                selected = PhysicalLocation::reg(candidate);
                                break;
                            }
                            if(probe.first_conflict.has_value())
                            {
                                std::optional<LivenessPosition> boundary =
                                    pressure_split_boundary(
                                        bundle, *probe.first_conflict);
                                uint64_t cost = maximum_conflict_weight(probe);
                                if(boundary.has_value() &&
                                   (!split_candidate.has_value() ||
                                    cost < split_candidate->conflict_cost))
                                {
                                    split_candidate =
                                        PressureSplitCandidate{*boundary, cost};
                                }
                            }
                            std::optional<uint64_t> cost = eviction_cost(probe);
                            if(cost.has_value() &&
                               *cost < bundle.spill_weight &&
                               (!lowest_eviction_cost.has_value() ||
                                *cost < *lowest_eviction_cost))
                            {
                                lowest_eviction_cost = cost;
                                evictable = candidate;
                                eviction_probe = std::move(probe);
                            }
                        }
                        if(!selected.has_value() && evictable.has_value())
                        {
                            evict(*eviction_probe);
                            selected = PhysicalLocation::reg(*evictable);
                        }
                        else if(!selected.has_value() &&
                                trim_spill_carrier(bundle_id))
                        {
                            continue;
                        }
                        else if(!selected.has_value() &&
                                split_candidate.has_value() &&
                                split_at_pressure_boundary(
                                    bundle_id, split_candidate->boundary))
                        {
                            continue;
                        }
                    }

                    if(!selected.has_value())
                    {
                        return Result<BundleAssignmentResult,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      RequiresSplittingOrSpilling);
                    }
                    place(bundle_id, *selected);
                }

                uint32_t spill_slot_count = assign_spill_slots();
                std::vector<BundleLocation> result;
                result.reserve(location_by_bundle_.size());
                for(const std::optional<BundleLocation> &location:
                    location_by_bundle_)
                {
                    if(!location.has_value())
                    {
                        fatal("JIT allocator left a bundle unassigned");
                    }
                    result.push_back(*location);
                }
                return Result<BundleAssignmentResult, RegisterAllocationError>::
                    ok({BundleLocationAssignments(std::move(result)),
                        spill_slot_count});
            }

        private:
            void enqueue_bundles()
            {
                for(size_t index = 0; index < bundles_.size(); ++index)
                {
                    enqueue(BundleId(index));
                }
            }

            void enqueue(BundleId bundle)
            {
                worklist_.push(
                    {bundle, bundles_[bundle.value()].allocation_priority});
            }

            std::optional<PhysicalLocation>
            required_location(const LiveBundle &bundle) const
            {
                std::optional<PhysicalLocation> result;
                for(FixedConstraintId fixed_id: bundle.fixed_constraints)
                {
                    PhysicalLocation location =
                        problem_.fixed_constraints()[fixed_id.value()].location;
                    if(result.has_value() && !result->aliases(location))
                    {
                        fatal("location-split JIT bundle has incompatible "
                              "fixed locations");
                    }
                    if(!result.has_value())
                    {
                        result = location;
                    }
                }
                return result;
            }

            const RegisterClassDefinition &
            register_class(RegisterClass register_class) const
            {
                for(const RegisterClassDefinition &definition:
                    constraints_.register_classes())
                {
                    if(definition.register_class() == register_class)
                    {
                        return definition;
                    }
                }
                fatal("JIT allocator has no definition for a required register "
                      "class");
            }

            RegisterProbe probe_register(PhysicalRegister reg,
                                         const LiveBundle &bundle) const
            {
                assert(reg.register_class() == bundle.register_class);
                RegisterProbe result;
                for(const BundleFragment &fragment: bundle.fragments)
                {
                    const RegisterOccupancy &occupancy = occupancy_[reg];
                    auto assigned = occupancy.lower_bound(fragment.range.start);
                    if(assigned != occupancy.begin())
                    {
                        auto previous = std::prev(assigned);
                        if(previous->second.end > fragment.range.start)
                        {
                            result.conflicting_bundles.push_back(
                                previous->second.bundle);
                            result.record_conflict(fragment.range.start);
                        }
                    }
                    for(; assigned != occupancy.end() &&
                          assigned->first < fragment.range.end;
                        ++assigned)
                    {
                        result.conflicting_bundles.push_back(
                            assigned->second.bundle);
                        LivenessPosition conflict =
                            std::max(fragment.range.start, assigned->first);
                        result.record_conflict(conflict);
                    }

                    const std::vector<LivenessRange> &clobbers =
                        clobber_ranges_[reg];
                    auto clobber = std::ranges::lower_bound(
                        clobbers, fragment.range.start, {},
                        [](LivenessRange range) { return range.start; });
                    if(clobber != clobbers.begin())
                    {
                        LivenessRange previous = *std::prev(clobber);
                        if(previous.end > fragment.range.start)
                        {
                            result.conflicts_with_clobber = true;
                            result.record_conflict(fragment.range.start);
                        }
                    }
                    for(; clobber != clobbers.end() &&
                          clobber->start < fragment.range.end;
                        ++clobber)
                    {
                        result.conflicts_with_clobber = true;
                        LivenessPosition conflict =
                            std::max(fragment.range.start, clobber->start);
                        result.record_conflict(conflict);
                    }
                }
                std::ranges::sort(result.conflicting_bundles);
                result.conflicting_bundles.erase(
                    std::unique(result.conflicting_bundles.begin(),
                                result.conflicting_bundles.end()),
                    result.conflicting_bundles.end());
                return result;
            }

            std::optional<uint64_t>
            eviction_cost(const RegisterProbe &probe) const
            {
                if(probe.conflicts_with_clobber ||
                   probe.conflicting_bundles.empty())
                {
                    return std::nullopt;
                }
                return maximum_conflict_weight(probe);
            }

            uint64_t maximum_conflict_weight(const RegisterProbe &probe) const
            {
                uint64_t result = 0;
                for(BundleId conflict: probe.conflicting_bundles)
                {
                    result = std::max(result,
                                      bundles_[conflict.value()].spill_weight);
                }
                return result;
            }

            const BlockLivenessRange *
            block_range_containing(LivenessPosition position) const
            {
                auto candidate = std::ranges::upper_bound(
                    problem_.block_ranges(), position, {},
                    [](const BlockLivenessRange &block_range) {
                        return block_range.range.start;
                    });
                if(candidate == problem_.block_ranges().begin())
                {
                    return nullptr;
                }
                --candidate;
                return candidate->range.contains(position) ? &*candidate
                                                           : nullptr;
            }

            std::optional<LivenessPosition>
            transfer_boundary_for_conflict(LivenessPosition position) const
            {
                const BlockLivenessRange *block_range =
                    block_range_containing(position);
                if(block_range == nullptr)
                {
                    return std::nullopt;
                }

                size_t offset =
                    position.value() - block_range->range.start.value();
                size_t exit_offset =
                    2 + block_range->block->instructions().size() * 2;
                if(offset < 2)
                {
                    offset = 2;
                }
                else if(offset <= exit_offset && offset % 2 != 0)
                {
                    --offset;
                }
                else if(offset > exit_offset)
                {
                    offset = exit_offset;
                }
                return LivenessPosition(block_range->range.start.value() +
                                        offset);
            }

            std::optional<TransferPoint>
            transfer_point_for_boundary(LivenessPosition boundary) const
            {
                const BlockLivenessRange *block_range =
                    block_range_containing(boundary);
                if(block_range == nullptr)
                {
                    return std::nullopt;
                }

                size_t offset =
                    boundary.value() - block_range->range.start.value();
                if(offset == 0)
                {
                    return TransferPoint::block_entry(block_range->block);
                }

                size_t instruction_count =
                    block_range->block->instructions().size();
                size_t exit_offset = 2 + instruction_count * 2;
                if(offset == exit_offset)
                {
                    return TransferPoint::block_exit(block_range->block);
                }
                if(offset < 2 || exit_offset <= offset || offset % 2 != 0)
                {
                    return std::nullopt;
                }
                return TransferPoint::before_instruction(
                    block_range->block->instruction_at((offset - 2) / 2));
            }

            std::optional<LivenessPosition>
            transfer_boundary_at_or_after(const BlockLivenessRange &block_range,
                                          LivenessPosition position) const
            {
                if(position <= block_range.range.start)
                {
                    return block_range.range.start;
                }
                if(block_range.range.end < position)
                {
                    return std::nullopt;
                }
                size_t offset =
                    position.value() - block_range.range.start.value();
                if(offset == 1)
                {
                    offset = 2;
                }
                else if(offset % 2 != 0)
                {
                    ++offset;
                }
                size_t exit_offset =
                    2 + block_range.block->instructions().size() * 2;
                return offset <= exit_offset
                           ? std::optional<LivenessPosition>(LivenessPosition(
                                 block_range.range.start.value() + offset))
                           : std::nullopt;
            }

            std::optional<LivenessPosition> transfer_boundary_at_or_before(
                const BlockLivenessRange &block_range,
                LivenessPosition position) const
            {
                if(position < block_range.range.start)
                {
                    return std::nullopt;
                }
                if(block_range.range.end <= position)
                {
                    return block_range.range.end;
                }
                size_t offset =
                    position.value() - block_range.range.start.value();
                if(offset == 1)
                {
                    offset = 0;
                }
                else if(offset % 2 != 0)
                {
                    --offset;
                }
                return LivenessPosition(block_range.range.start.value() +
                                        offset);
            }

            bool is_fixed_operand_copy_for(OccurrenceId occurrence,
                                           InstructionId instruction) const
            {
                const std::optional<InstructionId> &copy_instruction =
                    fixed_operand_copy_instruction_[occurrence.value()];
                return copy_instruction.has_value() &&
                       *copy_instruction == instruction;
            }

            std::optional<LivenessRange>
            spill_carrier_range(const LiveBundle &bundle,
                                const BundleFragment &crossing,
                                const SpillBoundary &spill_boundary) const
            {
                assert(crossing.range.contains(spill_boundary.range));

                const BlockLivenessRange *block_range =
                    block_range_containing(spill_boundary.range.start);
                if(block_range == nullptr ||
                   !block_range->range.contains(spill_boundary.range))
                {
                    return std::nullopt;
                }
                std::optional<LivenessPosition> start =
                    transfer_boundary_at_or_after(*block_range,
                                                  crossing.range.start);
                std::optional<LivenessPosition> end =
                    transfer_boundary_at_or_before(*block_range,
                                                   crossing.range.end);
                if(!start.has_value() || !end.has_value())
                {
                    return std::nullopt;
                }

                const LiveRange &source =
                    problem_.live_ranges()[crossing.source.value()];
                for(OccurrenceId occurrence_id: source.occurrences)
                {
                    const Occurrence &occurrence =
                        problem_.occurrences()[occurrence_id.value()];
                    if(!crossing.range.contains(occurrence.minimum_coverage) ||
                       is_fixed_operand_copy_for(occurrence_id,
                                                 spill_boundary.instruction))
                    {
                        continue;
                    }
                    if(occurrence.minimum_coverage.end <=
                       spill_boundary.range.start)
                    {
                        std::optional<LivenessPosition> candidate =
                            transfer_boundary_at_or_after(
                                *block_range, occurrence.minimum_coverage.end);
                        if(!candidate.has_value())
                        {
                            return std::nullopt;
                        }
                        start = std::max(*start, *candidate);
                    }
                    else if(spill_boundary.range.end <=
                            occurrence.minimum_coverage.start)
                    {
                        std::optional<LivenessPosition> candidate =
                            transfer_boundary_at_or_before(
                                *block_range,
                                occurrence.minimum_coverage.start);
                        if(!candidate.has_value())
                        {
                            return std::nullopt;
                        }
                        end = std::min(*end, *candidate);
                    }
                    else
                    {
                        return std::nullopt;
                    }
                }

                LivenessRange result{*start, *end};
                if(result.start > spill_boundary.range.start ||
                   result.end < spill_boundary.range.end || result.empty() ||
                   !can_split_bundle(bundle, problem_, result.start) ||
                   !can_split_bundle(bundle, problem_, result.end))
                {
                    return std::nullopt;
                }
                return result;
            }

            bool trim_spill_carrier(BundleId bundle_id)
            {
                assert(!location_by_bundle_[bundle_id.value()].has_value());
                const LiveBundle &bundle = bundles_[bundle_id.value()];
                for(const BundleFragment &fragment: bundle.fragments)
                {
                    auto boundary = std::ranges::lower_bound(
                        spill_boundaries_, fragment.range.start.next(), {},
                        [](const SpillBoundary &candidate) {
                            return candidate.range.end;
                        });
                    for(; boundary != spill_boundaries_.end() &&
                          boundary->range.start < fragment.range.end;
                        ++boundary)
                    {
                        if(!fragment.range.contains(boundary->range))
                        {
                            continue;
                        }
                        std::optional<LivenessRange> carrier =
                            spill_carrier_range(bundle, fragment, *boundary);
                        if(!carrier.has_value())
                        {
                            continue;
                        }
                        std::optional<TransferPoint> start_point =
                            transfer_point_for_boundary(carrier->start);
                        std::optional<TransferPoint> end_point =
                            transfer_point_for_boundary(carrier->end);
                        if(!start_point.has_value() || !end_point.has_value())
                        {
                            continue;
                        }

                        std::optional<BundleId> middle = split_bundle(
                            bundles_, transfers_, problem_, bundle_id,
                            carrier->start, *start_point);
                        assert(middle.has_value());
                        assert(middle->value() == location_by_bundle_.size());
                        location_by_bundle_.push_back(std::nullopt);
                        spill_candidate_by_bundle_.push_back(false);

                        std::optional<BundleId> right =
                            split_bundle(bundles_, transfers_, problem_,
                                         *middle, carrier->end, *end_point);
                        assert(right.has_value());
                        assert(right->value() == location_by_bundle_.size());
                        location_by_bundle_.push_back(std::nullopt);
                        spill_candidate_by_bundle_.push_back(false);
                        spill_candidate_by_bundle_[middle->value()] = true;

                        enqueue(bundle_id);
                        enqueue(*middle);
                        enqueue(*right);
                        return true;
                    }
                }
                return false;
            }

            std::optional<LivenessPosition>
            pressure_split_boundary(const LiveBundle &bundle,
                                    LivenessPosition first_conflict) const
            {
                assert(!bundle.fragments.empty());
                LivenessPosition bundle_start =
                    bundle.fragments.front().range.start;
                LivenessPosition bundle_end = bundle.fragments.back().range.end;

                std::optional<LivenessPosition> candidate =
                    transfer_boundary_for_conflict(first_conflict);
                if(!candidate.has_value())
                {
                    return std::nullopt;
                }
                if(*candidate <= bundle_start)
                {
                    candidate =
                        transfer_boundary_for_conflict(bundle_start.next());
                    if(candidate.has_value() &&
                       *candidate < bundle_start.next())
                    {
                        candidate = LivenessPosition(candidate->value() + 2);
                    }
                }
                return candidate.has_value() && *candidate < bundle_end
                           ? candidate
                           : std::nullopt;
            }

            bool split_for_pressure(BundleId bundle_id,
                                    const RegisterProbe &probe)
            {
                if(!probe.first_conflict.has_value())
                {
                    return false;
                }
                std::optional<LivenessPosition> boundary =
                    pressure_split_boundary(bundles_[bundle_id.value()],
                                            *probe.first_conflict);
                if(!boundary.has_value())
                {
                    return false;
                }
                return split_at_pressure_boundary(bundle_id, *boundary);
            }

            bool split_at_pressure_boundary(BundleId bundle_id,
                                            LivenessPosition boundary)
            {
                assert(!location_by_bundle_[bundle_id.value()].has_value());
                std::optional<TransferPoint> transfer_point =
                    transfer_point_for_boundary(boundary);
                if(!transfer_point.has_value())
                {
                    return false;
                }
                std::optional<BundleId> right =
                    split_bundle(bundles_, transfers_, problem_, bundle_id,
                                 boundary, *transfer_point);
                if(!right.has_value())
                {
                    return false;
                }
                assert(right->value() == location_by_bundle_.size());
                location_by_bundle_.push_back(std::nullopt);
                spill_candidate_by_bundle_.push_back(false);
                enqueue(bundle_id);
                enqueue(*right);
                return true;
            }

            void evict(const RegisterProbe &probe)
            {
                for(BundleId conflict: probe.conflicting_bundles)
                {
                    unplace(conflict);
                    enqueue(conflict);
                }
            }

            bool split_before_later_fixed_use(BundleId bundle_id,
                                              const RegisterProbe &probe)
            {
                assert(probe.first_conflict.has_value());
                const LiveBundle &bundle = bundles_[bundle_id.value()];
                std::optional<FixedConstraintId> selected;
                std::optional<LivenessPosition> selected_boundary;
                for(FixedConstraintId fixed_id: bundle.fixed_constraints)
                {
                    const FixedLocationConstraint &fixed =
                        problem_.fixed_constraints()[fixed_id.value()];
                    const Occurrence &occurrence =
                        problem_.occurrences()[fixed.occurrence.value()];
                    LivenessPosition boundary =
                        occurrence.minimum_coverage.start;
                    if(boundary <= *probe.first_conflict ||
                       (selected_boundary.has_value() &&
                        boundary >= *selected_boundary))
                    {
                        continue;
                    }
                    selected = fixed_id;
                    selected_boundary = boundary;
                }
                if(!selected.has_value())
                {
                    return false;
                }

                OccurrenceId occurrence =
                    problem_.fixed_constraints()[selected->value()].occurrence;
                std::optional<BundleId> right = split_bundle(
                    bundles_, transfers_, problem_, bundle_id,
                    *selected_boundary,
                    transfer_point_for_occurrence(problem_, occurrence));
                if(!right.has_value())
                {
                    return false;
                }
                assert(right->value() == location_by_bundle_.size());
                location_by_bundle_.push_back(std::nullopt);
                spill_candidate_by_bundle_.push_back(false);
                enqueue(bundle_id);
                enqueue(*right);
                return true;
            }

            void unplace(BundleId bundle_id)
            {
                std::optional<BundleLocation> &location =
                    location_by_bundle_[bundle_id.value()];
                assert(location.has_value() && location->is_register());
                RegisterOccupancy &occupancy = occupancy_[location->reg()];
                for(const BundleFragment &fragment:
                    bundles_[bundle_id.value()].fragments)
                {
                    auto found = occupancy.find(fragment.range.start);
                    assert(found != occupancy.end() &&
                           found->second.end == fragment.range.end &&
                           found->second.bundle == bundle_id);
                    occupancy.erase(found);
                }
                location.reset();
            }

            bool fits(StackLocation stack, const LiveBundle &bundle) const
            {
                auto found = stack_occupancy_.find(stack.frame_offset());
                if(found == stack_occupancy_.end())
                {
                    return true;
                }
                for(const BundleFragment &fragment: bundle.fragments)
                {
                    if(ranges_overlap(found->second, fragment.range))
                    {
                        return false;
                    }
                }
                return true;
            }

            void place(BundleId bundle_id, PhysicalLocation location)
            {
                assert(!location_by_bundle_[bundle_id.value()].has_value());
                location_by_bundle_[bundle_id.value()] =
                    BundleLocation::physical(location);

                RegisterOccupancy &occupancy =
                    location.is_register()
                        ? occupancy_[location.reg()]
                        : stack_occupancy_[location.stack().frame_offset()];
                for(const BundleFragment &fragment:
                    bundles_[bundle_id.value()].fragments)
                {
                    auto [position, inserted] = occupancy.emplace(
                        fragment.range.start,
                        AssignedFragment{fragment.range.end, bundle_id});
                    (void)position;
                    if(!inserted)
                    {
                        fatal("JIT allocator assigned two fragments with the "
                              "same start position to one register");
                    }
                }
            }

            void defer_spill_slot_assignment(BundleId bundle_id)
            {
                assert(!location_by_bundle_[bundle_id.value()].has_value());
                assert(bundles_[bundle_id.value()].fixed_constraints.empty());
                deferred_spill_bundles_.push_back(bundle_id);
            }

            uint32_t assign_spill_slots()
            {
                std::ranges::sort(
                    deferred_spill_bundles_, [&](BundleId lhs, BundleId rhs) {
                        const LiveBundle &left = bundles_[lhs.value()];
                        const LiveBundle &right = bundles_[rhs.value()];
                        assert(left.fragments.size() == 1);
                        assert(right.fragments.size() == 1);
                        LivenessPosition left_start =
                            left.fragments.front().range.start;
                        LivenessPosition right_start =
                            right.fragments.front().range.start;
                        return left_start != right_start
                                   ? left_start < right_start
                                   : lhs.value() < rhs.value();
                    });

                std::priority_queue<ActiveSpillSlot,
                                    std::vector<ActiveSpillSlot>,
                                    ActiveSpillSlotCompare>
                    active;
                std::set<uint32_t> free_slots;
                uint32_t slot_count = 0;
                for(BundleId bundle_id: deferred_spill_bundles_)
                {
                    const LiveBundle &bundle = bundles_[bundle_id.value()];
                    assert(bundle.fragments.size() == 1);
                    LivenessRange range = bundle.fragments.front().range;
                    while(!active.empty() && active.top().end <= range.start)
                    {
                        free_slots.insert(active.top().slot);
                        active.pop();
                    }

                    uint32_t slot;
                    if(free_slots.empty())
                    {
                        slot = slot_count++;
                    }
                    else
                    {
                        auto first = free_slots.begin();
                        slot = *first;
                        free_slots.erase(first);
                    }
                    location_by_bundle_[bundle_id.value()] =
                        BundleLocation::spill_slot(SpillSlotId(slot));
                    active.push({range.end, slot});
                }
                return slot_count;
            }

            const PreparedAllocationProblem &problem_;
            std::vector<LiveBundle> &bundles_;
            BundleTransferSchedule &transfers_;
            const AllocationConstraints &constraints_;
            std::vector<std::optional<BundleLocation>> location_by_bundle_;
            std::vector<bool> spill_candidate_by_bundle_;
            std::vector<BundleId> deferred_spill_bundles_;
            std::vector<SpillBoundary> spill_boundaries_;
            std::vector<std::optional<InstructionId>>
                fixed_operand_copy_instruction_;
            PerPhysicalRegister<RegisterOccupancy> occupancy_;
            std::unordered_map<int32_t, RegisterOccupancy> stack_occupancy_;
            PerPhysicalRegister<std::vector<LivenessRange>> clobber_ranges_;
            std::priority_queue<BundleWorkItem, std::vector<BundleWorkItem>,
                                BundleWorkItemCompare>
                worklist_;
        };
    }  // namespace

    Result<RegisterAllocationResult, RegisterAllocationError>
    assign_bundles(const PreparedAllocationProblem &problem,
                   const AllocationConstraints &constraints)
    {
        auto split_result = split_for_location_constraints(problem);
        if(!split_result)
        {
            return propagate_failure(std::move(split_result));
        }
        LocationConstraintSplit split = std::move(split_result).value();

        auto assignment_result =
            BundleAssigner(problem, split.bundles, split.transfers, constraints)
                .run();
        if(!assignment_result)
        {
            return propagate_failure(std::move(assignment_result));
        }
        BundleAssignmentResult assigned = std::move(assignment_result).value();
        BundleLocationAssignments &assignments = assigned.locations;
        schedule_affinity_transfers(problem, split.bundles, assignments,
                                    split.transfers);
        std::vector<FixedOperandCopyFixup> fixed_operand_copies;
        fixed_operand_copies.reserve(problem.fixed_operand_copies().size());
        for(const FixedOperandCopyConstraint &fixed_operand_copy:
            problem.fixed_operand_copies())
        {
            fixed_operand_copies.push_back(
                {fixed_operand_copy.source, fixed_operand_copy.destination});
        }
        RegisterAllocationResult allocation(
            std::move(split.bundles), std::move(assigned.locations),
            std::move(split.transfers), std::move(fixed_operand_copies),
            assigned.spill_slot_count);
        verify_register_allocation(problem, constraints, allocation);
        return Result<RegisterAllocationResult, RegisterAllocationError>::ok(
            std::move(allocation));
    }

}  // namespace cl::jit
