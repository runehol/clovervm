#ifndef CL_MODULE_GLOBAL_H
#define CL_MODULE_GLOBAL_H

#include "import_system/module_global_descriptor.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"

namespace cl
{
    class ModuleObject;
    class String;

    ModuleGlobalReadDescriptor
    resolve_module_global_read_descriptor(ModuleObject *module,
                                          TValue<String> name);
    ModuleGlobalWriteDescriptor
    resolve_module_global_write_descriptor(ModuleObject *module,
                                           TValue<String> name);
    ModuleGlobalDeleteDescriptor
    resolve_module_global_delete_descriptor(ModuleObject *module,
                                            TValue<String> name);

    Value load_module_global_from_slot_plan(const ModuleGlobalSlotPlan &plan);
    Value load_module_global_from_plan(const ModuleGlobalReadPlan &plan);
    bool store_module_global_from_store_existing_plan(
        const ModuleGlobalStoreExistingPlan &plan, Value value);
    bool store_module_global_from_plan(ModuleObject *module,
                                       const ModuleGlobalMutationPlan &plan,
                                       Value value);
    bool delete_module_global_from_plan(ModuleObject *module,
                                        const ModuleGlobalMutationPlan &plan);

    Value load_module_global(ModuleObject *module, TValue<String> name);
    bool store_module_global(ModuleObject *module, TValue<String> name,
                             Value value);
    bool delete_module_global(ModuleObject *module, TValue<String> name);

}  // namespace cl

#endif  // CL_MODULE_GLOBAL_H
