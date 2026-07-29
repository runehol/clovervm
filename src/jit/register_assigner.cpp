#include "jit/register_allocator_internal.h"

#include "runtime/fatal.h"

#include <absl/container/btree_map.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <iterator>
#include <optional>
#include <queue>
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
                  constraints_(constraints), location_by_bundle_(bundles.size())
            {
                for(const ClobberReservation &clobber: problem_.clobbers())
                {
                    clobber_ranges_[clobber.reg].push_back(clobber.range);
                }
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

            Result<BundleLocationAssignments, RegisterAllocationError> run()
            {
                enqueue_bundles();
                while(!worklist_.empty())
                {
                    BundleId bundle_id = worklist_.top().bundle;
                    worklist_.pop();
                    const LiveBundle &bundle = bundles_[bundle_id.value()];

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
                    }
                    else
                    {
                        std::optional<uint64_t> lowest_eviction_cost;
                        std::optional<PhysicalRegister> evictable;
                        std::optional<RegisterProbe> eviction_probe;
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
                    }

                    if(!selected.has_value())
                    {
                        return Result<BundleLocationAssignments,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      RequiresSplittingOrSpilling);
                    }
                    place(bundle_id, *selected);
                }

                std::vector<PhysicalLocation> result;
                result.reserve(location_by_bundle_.size());
                for(const std::optional<PhysicalLocation> &location:
                    location_by_bundle_)
                {
                    if(!location.has_value())
                    {
                        fatal("JIT allocator left a bundle unassigned");
                    }
                    result.push_back(*location);
                }
                return Result<BundleLocationAssignments,
                              RegisterAllocationError>::
                    ok(BundleLocationAssignments(std::move(result)));
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
                uint64_t result = 0;
                for(BundleId conflict: probe.conflicting_bundles)
                {
                    result = std::max(result,
                                      bundles_[conflict.value()].spill_weight);
                }
                return result;
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
                enqueue(bundle_id);
                enqueue(*right);
                return true;
            }

            void unplace(BundleId bundle_id)
            {
                std::optional<PhysicalLocation> &location =
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
                location_by_bundle_[bundle_id.value()] = location;

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

            const PreparedAllocationProblem &problem_;
            std::vector<LiveBundle> &bundles_;
            BundleTransferSchedule &transfers_;
            const AllocationConstraints &constraints_;
            std::vector<std::optional<PhysicalLocation>> location_by_bundle_;
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
        BundleLocationAssignments assignments =
            std::move(assignment_result).value();
        RegisterAllocationResult allocation(std::move(split.bundles),
                                            std::move(assignments),
                                            std::move(split.transfers));
        verify_register_allocation(problem, constraints, allocation);
        return Result<RegisterAllocationResult, RegisterAllocationError>::ok(
            std::move(allocation));
    }

}  // namespace cl::jit
