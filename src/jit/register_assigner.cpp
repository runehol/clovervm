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

        bool ranges_overlap(const std::vector<LivenessRange> &ranges,
                            LivenessRange candidate)
        {
            auto position = std::ranges::lower_bound(
                ranges, candidate.start, {},
                [](LivenessRange range) { return range.start; });
            if(position != ranges.begin() &&
               std::prev(position)->end > candidate.start)
            {
                return true;
            }
            return position != ranges.end() && position->start < candidate.end;
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
                           std::span<const LiveBundle> bundles,
                           const AllocationConstraints &constraints)
                : problem_(problem), bundles_(bundles),
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

                    std::optional<AllocationLocation> selected;
                    std::optional<AllocationLocation> required =
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
                        if(fits(required->reg(), bundle))
                        {
                            selected = required;
                        }
                    }
                    else
                    {
                        for(PhysicalRegister candidate:
                            register_class(bundle.register_class)
                                .allocation_order())
                        {
                            if(fits(candidate, bundle))
                            {
                                selected = AllocationLocation::reg(candidate);
                                break;
                            }
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

                std::vector<AllocationLocation> result;
                result.reserve(location_by_bundle_.size());
                for(const std::optional<AllocationLocation> &location:
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
                    const LiveBundle &bundle = bundles_[index];
                    worklist_.push(
                        {BundleId(index), bundle.allocation_priority});
                }
            }

            std::optional<AllocationLocation>
            required_location(const LiveBundle &bundle) const
            {
                std::optional<AllocationLocation> result;
                for(FixedConstraintId fixed_id: bundle.fixed_constraints)
                {
                    AllocationLocation location =
                        problem_.fixed_constraints()[fixed_id.value()].location;
                    if(result.has_value() && !result->aliases(location))
                    {
                        fatal("normalized JIT bundle has incompatible fixed "
                              "locations");
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

            bool fits(PhysicalRegister reg, const LiveBundle &bundle) const
            {
                assert(reg.register_class() == bundle.register_class);
                for(const BundleFragment &fragment: bundle.fragments)
                {
                    if(ranges_overlap(occupancy_[reg], fragment.range) ||
                       ranges_overlap(clobber_ranges_[reg], fragment.range))
                    {
                        return false;
                    }
                }
                return true;
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

            void place(BundleId bundle_id, AllocationLocation location)
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
            std::span<const LiveBundle> bundles_;
            const AllocationConstraints &constraints_;
            std::vector<std::optional<AllocationLocation>> location_by_bundle_;
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
        auto normalized_result = normalize_bundle_constraints(problem);
        if(!normalized_result)
        {
            return propagate_failure(std::move(normalized_result));
        }
        NormalizedBundles normalized = std::move(normalized_result).value();

        auto assignment_result =
            BundleAssigner(problem, normalized.bundles, constraints).run();
        if(!assignment_result)
        {
            return propagate_failure(std::move(assignment_result));
        }
        BundleLocationAssignments assignments =
            std::move(assignment_result).value();
        RegisterAllocationResult allocation(std::move(normalized.bundles),
                                            std::move(assignments),
                                            std::move(normalized.transfers));
        verify_register_allocation(problem, constraints, allocation);
        return Result<RegisterAllocationResult, RegisterAllocationError>::ok(
            std::move(allocation));
    }

}  // namespace cl::jit
