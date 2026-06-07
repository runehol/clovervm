#ifndef CL_MODULE_GLOBAL_CACHE_H
#define CL_MODULE_GLOBAL_CACHE_H

#include "import_system/module_global_descriptor.h"
#include "object_model/validity_cell.h"
#include <cassert>

namespace cl
{
    class ModuleGlobalReadInlineCache
    {
    public:
        ValidityCell *lookup_validity_cell = nullptr;
        ModuleGlobalSlotPlan slot = ModuleGlobalSlotPlan::not_found();

        ALWAYSINLINE bool matches() const
        {
            return lookup_validity_cell != nullptr &&
                   lookup_validity_cell->is_valid();
        }

        void populate(const ModuleGlobalReadDescriptor &descriptor)
        {
            assert(descriptor.is_cacheable());
            assert(descriptor.plan.kind == ModuleGlobalReadPlanKind::Slot);
            lookup_validity_cell = descriptor.lookup_validity_cell;
            slot = descriptor.plan.slot_plan;
        }

        void clear()
        {
            lookup_validity_cell = nullptr;
            slot = ModuleGlobalSlotPlan::not_found();
        }
    };

    class ModuleGlobalMutationInlineCache
    {
    public:
        ValidityCell *lookup_validity_cell = nullptr;
        ModuleGlobalStoreExistingPlan store_existing =
            ModuleGlobalStoreExistingPlan::not_found();

        ALWAYSINLINE bool matches() const
        {
            return lookup_validity_cell != nullptr &&
                   lookup_validity_cell->is_valid();
        }

        void populate(const ModuleGlobalWriteDescriptor &descriptor)
        {
            assert(descriptor.is_cacheable());
            assert(descriptor.plan.kind ==
                   ModuleGlobalMutationPlanKind::StoreExisting);
            lookup_validity_cell = descriptor.lookup_validity_cell;
            store_existing = descriptor.plan.store_existing_plan;
        }

        void clear()
        {
            lookup_validity_cell = nullptr;
            store_existing = ModuleGlobalStoreExistingPlan::not_found();
        }
    };

}  // namespace cl

#endif  // CL_MODULE_GLOBAL_CACHE_H
