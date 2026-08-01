#include "jit/register_allocator_internal.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        struct LocationDomain
        {
            bool register_required = false;
            std::optional<PhysicalLocation> fixed;

            bool add_register_requirement()
            {
                if(fixed.has_value() && fixed->is_stack())
                {
                    return false;
                }
                register_required = true;
                return true;
            }

            bool add_fixed(PhysicalLocation location)
            {
                if(location.is_register())
                {
                    if(fixed.has_value() && (!fixed->is_register() ||
                                             fixed->reg() != location.reg()))
                    {
                        return false;
                    }
                    register_required = true;
                    fixed = location;
                    return true;
                }

                if(register_required ||
                   (fixed.has_value() &&
                    (!fixed->is_stack() ||
                     !fixed->stack().aliases(location.stack()))))
                {
                    return false;
                }
                if(!fixed.has_value())
                {
                    fixed = location;
                }
                return true;
            }
        };

        std::optional<PhysicalLocation>
        fixed_location_for(const PreparedAllocationProblem &problem,
                           OccurrenceId occurrence_id)
        {
            std::optional<PhysicalLocation> result;
            for(const FixedLocationConstraint &fixed:
                problem.fixed_constraints())
            {
                if(fixed.occurrence != occurrence_id)
                {
                    continue;
                }
                if(result.has_value())
                {
                    fatal("multiple fixed locations for one JIT occurrence");
                }
                result = fixed.location;
            }
            return result;
        }

        std::vector<OccurrenceId>
        covered_occurrences(const LiveBundle &bundle,
                            const PreparedAllocationProblem &problem)
        {
            std::vector<OccurrenceId> result;
            for(const BundleFragment &fragment: bundle.fragments)
            {
                const LiveRange &source =
                    problem.live_ranges()[fragment.source.value()];
                for(OccurrenceId occurrence_id: source.occurrences)
                {
                    if(fragment.range.contains(
                           problem.occurrences()[occurrence_id.value()]
                               .minimum_coverage))
                    {
                        result.push_back(occurrence_id);
                    }
                }
            }
            std::ranges::sort(result,
                              OccurrencePositionLess(problem.occurrences()));
            return result;
        }

        class LocationConstraintSplitter
        {
        public:
            explicit LocationConstraintSplitter(
                const PreparedAllocationProblem &problem)
                : problem_(problem), bundles_(problem.bundles())
            {
            }

            Result<LocationConstraintSplit, RegisterAllocationError> run()
            {
                size_t initial_bundle_count = bundles_.size();
                for(size_t index = 0; index < initial_bundle_count; ++index)
                {
                    auto result = normalize_bundle(BundleId(index));
                    if(!result)
                    {
                        return propagate_failure(std::move(result));
                    }
                }
                return Result<LocationConstraintSplit,
                              RegisterAllocationError>::ok({std::move(bundles_),
                                                            std::move(
                                                                transfers_)});
            }

        private:
            Result<void, RegisterAllocationError>
            normalize_bundle(BundleId first_bundle)
            {
                BundleId current = first_bundle;
                while(true)
                {
                    LocationDomain domain;
                    bool split = false;
                    for(OccurrenceId occurrence_id: covered_occurrences(
                            bundles_[current.value()], problem_))
                    {
                        std::optional<PhysicalLocation> fixed =
                            fixed_location_for(problem_, occurrence_id);
                        const Occurrence &occurrence =
                            problem_.occurrences()[occurrence_id.value()];
                        bool compatible =
                            fixed.has_value()
                                ? domain.add_fixed(*fixed)
                                : !occurrence.register_required ||
                                      domain.add_register_requirement();
                        if(compatible)
                        {
                            continue;
                        }

                        std::optional<BundleId> right = cl::jit::split_bundle(
                            bundles_, transfers_, problem_, current,
                            occurrence.minimum_coverage.start,
                            transfer_point_for_occurrence(problem_,
                                                          occurrence_id));
                        if(!right.has_value())
                        {
                            return Result<void, RegisterAllocationError>::error(
                                RegisterAllocationError::
                                    RequiresConstraintFixup);
                        }
                        current = *right;
                        split = true;
                        break;
                    }
                    if(!split)
                    {
                        return Result<void, RegisterAllocationError>::ok();
                    }
                }
            }

            const PreparedAllocationProblem &problem_;
            std::vector<LiveBundle> bundles_;
            BundleTransferSchedule transfers_;
        };
    }  // namespace

    Result<LocationConstraintSplit, RegisterAllocationError>
    split_for_location_constraints(const PreparedAllocationProblem &problem)
    {
        return LocationConstraintSplitter(problem).run();
    }

}  // namespace cl::jit
