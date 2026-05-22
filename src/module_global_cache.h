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
        ModuleGlobalReadPlan plan =
            ModuleGlobalReadDescriptor::not_found().plan;

        ALWAYSINLINE bool matches() const
        {
            return plan.lookup_validity_cell != nullptr &&
                   plan.lookup_validity_cell->is_valid();
        }

        void populate(const ModuleGlobalReadDescriptor &descriptor)
        {
            assert(descriptor.is_cacheable());
            assert(descriptor.plan.kind == ModuleGlobalReadPlanKind::Slot);
            plan = descriptor.plan;
        }

        void clear() { plan = ModuleGlobalReadDescriptor::not_found().plan; }
    };

    class ModuleGlobalMutationInlineCache
    {
    public:
        ModuleGlobalMutationPlan plan =
            ModuleGlobalWriteDescriptor::not_found().plan;

        ALWAYSINLINE bool matches() const
        {
            return plan.lookup_validity_cell != nullptr &&
                   plan.lookup_validity_cell->is_valid();
        }

        void populate(const ModuleGlobalWriteDescriptor &descriptor)
        {
            assert(descriptor.is_cacheable());
            assert(descriptor.plan.kind ==
                   ModuleGlobalMutationPlanKind::StoreExisting);
            plan = descriptor.plan;
        }

        void clear() { plan = ModuleGlobalWriteDescriptor::not_found().plan; }
    };

}  // namespace cl

#endif  // CL_MODULE_GLOBAL_CACHE_H
