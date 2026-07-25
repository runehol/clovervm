#include "jit/register_allocator_internal.h"

#include "runtime/fatal.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <queue>
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
            ProgramRange range;
            BundleId bundle;
        };

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

        bool ranges_overlap(ProgramRange lhs, ProgramRange rhs)
        {
            return lhs.start < rhs.end && rhs.start < lhs.end;
        }

        class BundleAssigner
        {
        public:
            BundleAssigner(const PreparedAllocationProblem &problem,
                           const AllocationConstraints &constraints)
                : problem_(problem), constraints_(constraints),
                  register_by_bundle_(problem.bundles().size())
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
                        std::ranges::sort(
                            clobber_ranges_[reg],
                            [](ProgramRange lhs, ProgramRange rhs) {
                                return lhs.start < rhs.start;
                            });
                    }
                }
            }

            Result<BundleRegisterAssignments, RegisterAllocationError> run()
            {
                enqueue_bundles();
                while(!worklist_.empty())
                {
                    BundleId bundle_id = worklist_.top().bundle;
                    worklist_.pop();
                    const LiveBundle &bundle =
                        problem_.bundles()[bundle_id.value()];

                    auto required = required_register(bundle);
                    if(!required)
                    {
                        return propagate_failure(std::move(required));
                    }

                    std::optional<PhysicalRegister> selected;
                    if(required.value().has_value())
                    {
                        PhysicalRegister candidate = *required.value();
                        if(fits(candidate, bundle))
                        {
                            selected = candidate;
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
                                selected = candidate;
                                break;
                            }
                        }
                    }

                    if(!selected.has_value())
                    {
                        return Result<BundleRegisterAssignments,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      RequiresSplittingOrSpilling);
                    }
                    place(bundle_id, *selected);
                }

                std::vector<PhysicalRegister> result;
                result.reserve(register_by_bundle_.size());
                for(const std::optional<PhysicalRegister> &reg:
                    register_by_bundle_)
                {
                    if(!reg.has_value())
                    {
                        fatal("JIT allocator left a bundle unassigned");
                    }
                    result.push_back(*reg);
                }
                return Result<BundleRegisterAssignments,
                              RegisterAllocationError>::
                    ok(BundleRegisterAssignments(std::move(result)));
            }

        private:
            void enqueue_bundles()
            {
                for(size_t index = 0; index < problem_.bundles().size();
                    ++index)
                {
                    const LiveBundle &bundle = problem_.bundles()[index];
                    worklist_.push(
                        {BundleId(index), bundle.allocation_priority});
                }
            }

            Result<std::optional<PhysicalRegister>, RegisterAllocationError>
            required_register(const LiveBundle &bundle) const
            {
                std::optional<PhysicalRegister> result;
                for(FixedConstraintId fixed_id: bundle.fixed_constraints)
                {
                    PhysicalRegister reg =
                        problem_.fixed_constraints()[fixed_id.value()].reg;
                    if(result.has_value() && *result != reg)
                    {
                        return Result<std::optional<PhysicalRegister>,
                                      RegisterAllocationError>::
                            error(RegisterAllocationError::
                                      RequiresConstraintFixup);
                    }
                    result = reg;
                }
                return Result<std::optional<PhysicalRegister>,
                              RegisterAllocationError>::ok(result);
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
                    for(const AssignedFragment &assigned:
                        assigned_fragments_[reg])
                    {
                        if(ranges_overlap(fragment.range, assigned.range))
                        {
                            return false;
                        }
                    }
                    for(ProgramRange clobber: clobber_ranges_[reg])
                    {
                        if(ranges_overlap(fragment.range, clobber))
                        {
                            return false;
                        }
                    }
                }
                return true;
            }

            void place(BundleId bundle_id, PhysicalRegister reg)
            {
                assert(!register_by_bundle_[bundle_id.value()].has_value());
                register_by_bundle_[bundle_id.value()] = reg;

                std::vector<AssignedFragment> &assigned =
                    assigned_fragments_[reg];
                for(const BundleFragment &fragment:
                    problem_.bundles()[bundle_id.value()].fragments)
                {
                    auto position = std::ranges::lower_bound(
                        assigned, fragment.range.start, {},
                        [](const AssignedFragment &entry) {
                            return entry.range.start;
                        });
                    assigned.insert(
                        position, AssignedFragment{fragment.range, bundle_id});
                }
            }

            const PreparedAllocationProblem &problem_;
            const AllocationConstraints &constraints_;
            std::vector<std::optional<PhysicalRegister>> register_by_bundle_;
            PerPhysicalRegister<std::vector<AssignedFragment>>
                assigned_fragments_;
            PerPhysicalRegister<std::vector<ProgramRange>> clobber_ranges_;
            std::priority_queue<BundleWorkItem, std::vector<BundleWorkItem>,
                                BundleWorkItemCompare>
                worklist_;
        };
    }  // namespace

    Result<BundleRegisterAssignments, RegisterAllocationError>
    assign_bundles(const PreparedAllocationProblem &problem,
                   const AllocationConstraints &constraints)
    {
        auto result = BundleAssigner(problem, constraints).run();
        if(result)
        {
            verify_bundle_assignments(problem, constraints, result.value());
        }
        return result;
    }

}  // namespace cl::jit
