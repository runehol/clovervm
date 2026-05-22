#ifndef CL_MODULE_GLOBAL_CACHE_H
#define CL_MODULE_GLOBAL_CACHE_H

#include "module_global_descriptor.h"
#include "validity_cell.h"
#include <cassert>

namespace cl
{
    class ModuleGlobalReadInlineCache
    {
    public:
        ModuleGlobalSlotPlan slot = ModuleGlobalSlotPlan::not_found();

        ALWAYSINLINE bool matches() const
        {
            return slot.lookup_validity_cell != nullptr &&
                   slot.lookup_validity_cell->is_valid();
        }

        void populate(const ModuleGlobalReadDescriptor &descriptor)
        {
            assert(descriptor.is_cacheable());
            assert(descriptor.plan.kind == ModuleGlobalReadPlanKind::Slot);
            slot = descriptor.plan.slot_plan;
        }

        void clear() { slot = ModuleGlobalSlotPlan::not_found(); }
    };

    class ModuleGlobalMutationInlineCache
    {
    public:
        ModuleGlobalStoreExistingPlan store_existing =
            ModuleGlobalStoreExistingPlan::not_found();

        ALWAYSINLINE bool matches() const
        {
            return store_existing.lookup_validity_cell != nullptr &&
                   store_existing.lookup_validity_cell->is_valid();
        }

        void populate(const ModuleGlobalWriteDescriptor &descriptor)
        {
            assert(descriptor.is_cacheable());
            assert(descriptor.plan.kind ==
                   ModuleGlobalMutationPlanKind::StoreExisting);
            store_existing = descriptor.plan.store_existing_plan;
        }

        void clear()
        {
            store_existing = ModuleGlobalStoreExistingPlan::not_found();
        }
    };

}  // namespace cl

#endif  // CL_MODULE_GLOBAL_CACHE_H
