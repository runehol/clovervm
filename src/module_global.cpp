#include "module_global.h"
#include "module_object.h"
#include "shape.h"
#include "shape_descriptor.h"

namespace cl
{
    ModuleGlobalReadDescriptor
    resolve_module_global_read_descriptor(ModuleObject *module,
                                          TValue<String> name)
    {
        assert(module != nullptr);

        StorageLocation module_location =
            module->get_shape()->resolve_present_property(name);
        if(module_location.is_found())
        {
            ValidityCell *cell =
                module->get_or_create_module_globals_validity_cell();
            return ModuleGlobalReadDescriptor::found(
                ModuleGlobalReadPlan::slot(ModuleGlobalReadPlanKind::ModuleSlot,
                                           module, module_location, cell),
                module->read_storage_location(module_location));
        }

        ModuleBuiltinsLookup builtins_lookup =
            module->get_module_builtins_lookup();
        if(!builtins_lookup.is_module())
        {
            return ModuleGlobalReadDescriptor::uncacheable_builtins_object(
                builtins_lookup.builtins_object);
        }

        ModuleObject *builtins_module = builtins_lookup.builtins_module;
        ValidityCell *cell = builtins_lookup.lookup_validity_cell;

        StorageLocation builtins_location =
            builtins_module->get_shape()->resolve_present_property(name);
        if(!builtins_location.is_found())
        {
            return ModuleGlobalReadDescriptor::not_found(cell);
        }

        return ModuleGlobalReadDescriptor::found(
            ModuleGlobalReadPlan::slot(
                ModuleGlobalReadPlanKind::BuiltinsModuleSlot, builtins_module,
                builtins_location, cell),
            builtins_module->read_storage_location(builtins_location));
    }

    ModuleGlobalWriteDescriptor
    resolve_module_global_write_descriptor(ModuleObject *module,
                                           TValue<String> name)
    {
        assert(module != nullptr);
        Shape *shape = module->get_shape();
        if(!shape->allows_attribute_updates())
        {
            return ModuleGlobalWriteDescriptor::disallowed();
        }

        DescriptorLookup descriptor =
            shape->lookup_descriptor_including_latent(name);
        if(descriptor.is_present())
        {
            if(descriptor.info.has_flag(DescriptorFlag::ReadOnly))
            {
                return ModuleGlobalWriteDescriptor::read_only();
            }

            return ModuleGlobalWriteDescriptor::found(
                ModuleGlobalMutationPlan::store_existing(
                    module, descriptor.info.storage_location(),
                    module->get_or_create_module_globals_validity_cell()));
        }
        if(descriptor.is_latent())
        {
            return descriptor.info.has_flag(DescriptorFlag::ReadOnly)
                       ? ModuleGlobalWriteDescriptor::read_only()
                       : ModuleGlobalWriteDescriptor::not_found();
        }
        if(!shape->allows_attribute_add_delete())
        {
            return ModuleGlobalWriteDescriptor::disallowed();
        }

        Shape *next_shape =
            shape->derive_transition(name, ShapeTransitionVerb::Add);
        StorageLocation location = next_shape->resolve_present_property(name);
        assert(location.is_found());
        return ModuleGlobalWriteDescriptor::found(
            ModuleGlobalMutationPlan::add_own_property(next_shape, location));
    }

    ModuleGlobalDeleteDescriptor
    resolve_module_global_delete_descriptor(ModuleObject *module,
                                            TValue<String> name)
    {
        assert(module != nullptr);
        Shape *shape = module->get_shape();
        if(!shape->allows_attribute_add_delete())
        {
            return ModuleGlobalDeleteDescriptor::disallowed();
        }

        DescriptorLookup descriptor =
            shape->lookup_descriptor_including_latent(name);
        if(!descriptor.is_present())
        {
            return descriptor.is_latent() &&
                           descriptor.info.has_flag(DescriptorFlag::ReadOnly)
                       ? ModuleGlobalDeleteDescriptor::read_only()
                       : ModuleGlobalDeleteDescriptor::not_found();
        }
        if(descriptor.info.has_flag(DescriptorFlag::ReadOnly))
        {
            return ModuleGlobalDeleteDescriptor::read_only();
        }

        Shape *next_shape =
            shape->derive_transition(name, ShapeTransitionVerb::Delete);
        return ModuleGlobalDeleteDescriptor::found(
            ModuleGlobalMutationPlan::delete_own_property(
                next_shape, descriptor.info.storage_location()));
    }

    Value load_module_global_from_plan(const ModuleGlobalReadPlan &plan)
    {
        switch(plan.kind)
        {
            case ModuleGlobalReadPlanKind::ModuleSlot:
            case ModuleGlobalReadPlanKind::BuiltinsModuleSlot:
                assert(plan.storage_owner != nullptr);
                return plan.storage_owner->read_storage_location(
                    plan.storage_location);
            case ModuleGlobalReadPlanKind::Missing:
            case ModuleGlobalReadPlanKind::UncacheableBuiltinsObject:
                return Value::not_present();
        }
        __builtin_unreachable();
    }

    bool store_module_global_from_plan(ModuleObject *module,
                                       const ModuleGlobalMutationPlan &plan,
                                       Value value)
    {
        value.assert_not_vm_sentinel();
        switch(plan.kind)
        {
            case ModuleGlobalMutationPlanKind::StoreExisting:
                {
                    ModuleObject *storage_owner = plan.storage_owner;
                    assert(storage_owner != nullptr);
                    storage_owner->write_storage_location(
                        plan.storage_location(), value);
                    return true;
                }
            case ModuleGlobalMutationPlanKind::AddOwnProperty:
                {
                    assert(module != nullptr);
                    assert(plan.next_shape != nullptr);
                    assert(plan.storage_kind == StorageKind::Inline);
                    module->set_shape(plan.next_shape);
                    module->write_empty_storage_location(
                        plan.storage_location(), value);
                    return true;
                }
            case ModuleGlobalMutationPlanKind::DeleteOwnProperty:
                return false;
        }
        __builtin_unreachable();
    }

    bool delete_module_global_from_plan(ModuleObject *module,
                                        const ModuleGlobalMutationPlan &plan)
    {
        if(plan.kind != ModuleGlobalMutationPlanKind::DeleteOwnProperty)
        {
            return false;
        }
        assert(module != nullptr);
        assert(plan.next_shape != nullptr);
        module->set_shape(plan.next_shape);
        module->write_storage_location(plan.storage_location(),
                                       Value::not_present());
        return true;
    }

    Value load_module_global(ModuleObject *module, TValue<String> name)
    {
        ModuleGlobalReadDescriptor descriptor =
            resolve_module_global_read_descriptor(module, name);
        return load_module_global_from_plan(descriptor.plan);
    }

    bool store_module_global(ModuleObject *module, TValue<String> name,
                             Value value)
    {
        ModuleGlobalWriteDescriptor descriptor =
            resolve_module_global_write_descriptor(module, name);
        if(!descriptor.is_found())
        {
            return false;
        }
        return store_module_global_from_plan(module, descriptor.plan, value);
    }

    bool delete_module_global(ModuleObject *module, TValue<String> name)
    {
        ModuleGlobalDeleteDescriptor descriptor =
            resolve_module_global_delete_descriptor(module, name);
        if(!descriptor.is_found())
        {
            return false;
        }
        return delete_module_global_from_plan(module, descriptor.plan);
    }

}  // namespace cl
