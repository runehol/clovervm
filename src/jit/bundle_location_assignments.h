#ifndef CL_JIT_BUNDLE_LOCATION_ASSIGNMENTS_H
#define CL_JIT_BUNDLE_LOCATION_ASSIGNMENTS_H

#include "jit/allocation_problem.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace cl::jit
{
    class SpillSlotId
    {
    public:
        explicit constexpr SpillSlotId(uint32_t value) : value_(value) {}

        constexpr uint32_t value() const { return value_; }

        friend bool operator==(SpillSlotId, SpillSlotId) = default;

    private:
        uint32_t value_;
    };

    class BundleLocation
    {
    public:
        static BundleLocation physical(PhysicalLocation location)
        {
            return BundleLocation(location);
        }

        static BundleLocation spill_slot(SpillSlotId slot)
        {
            return BundleLocation(slot);
        }

        bool is_physical() const
        {
            return std::holds_alternative<PhysicalLocation>(storage_);
        }

        bool is_spill_slot() const
        {
            return std::holds_alternative<SpillSlotId>(storage_);
        }

        PhysicalLocation physical() const
        {
            assert(is_physical());
            return std::get<PhysicalLocation>(storage_);
        }

        SpillSlotId spill_slot() const
        {
            assert(is_spill_slot());
            return std::get<SpillSlotId>(storage_);
        }

        bool is_register() const
        {
            return is_physical() && physical().is_register();
        }

        bool is_stack() const { return is_physical() && physical().is_stack(); }

        PhysicalRegister reg() const { return physical().reg(); }
        StackLocation stack() const { return physical().stack(); }

        bool aliases(const BundleLocation &other) const
        {
            if(is_physical() && other.is_physical())
            {
                return physical().aliases(other.physical());
            }
            return is_spill_slot() && other.is_spill_slot() &&
                   spill_slot() == other.spill_slot();
        }

    private:
        explicit BundleLocation(PhysicalLocation location) : storage_(location)
        {
        }
        explicit BundleLocation(SpillSlotId slot) : storage_(slot) {}

        std::variant<PhysicalLocation, SpillSlotId> storage_;
    };

    class BundleLocationAssignments
    {
    public:
        explicit BundleLocationAssignments(
            std::vector<BundleLocation> locations)
            : locations_(std::move(locations))
        {
        }

        size_t size() const { return locations_.size(); }

        BundleLocation location_for(BundleId bundle) const
        {
            assert(bundle.value() < locations_.size());
            return locations_[bundle.value()];
        }

        PhysicalLocation physical_location_for(BundleId bundle) const
        {
            return location_for(bundle).physical();
        }

    private:
        std::vector<BundleLocation> locations_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_BUNDLE_LOCATION_ASSIGNMENTS_H
