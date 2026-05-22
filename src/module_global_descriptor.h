#ifndef CL_MODULE_GLOBAL_DESCRIPTOR_H
#define CL_MODULE_GLOBAL_DESCRIPTOR_H

#include "shape_descriptor.h"
#include "value.h"
#include <cassert>
#include <cstdint>

namespace cl
{
    class ModuleObject;
    class Shape;
    class ValidityCell;

    enum class ModuleGlobalReadStatus : uint8_t
    {
        Found = 0,
        NotFound,
        UncacheableBuiltinsObject,
        Error,
    };

    static_assert(static_cast<uint8_t>(ModuleGlobalReadStatus::Found) == 0);

    enum class ModuleGlobalWriteStatus : uint8_t
    {
        Found = 0,
        NotFound,
        ReadOnly,
        Disallowed,
        Error,
    };

    static_assert(static_cast<uint8_t>(ModuleGlobalWriteStatus::Found) == 0);

    enum class ModuleGlobalDeleteStatus : uint8_t
    {
        Found = 0,
        NotFound,
        ReadOnly,
        Disallowed,
        Error,
    };

    static_assert(static_cast<uint8_t>(ModuleGlobalDeleteStatus::Found) == 0);

    enum class ModuleGlobalReadPlanKind : uint8_t
    {
        Slot,
        Missing,
        UncacheableBuiltinsObject,
    };

    enum class ModuleGlobalMutationPlanKind : uint8_t
    {
        StoreExisting,
        AddOwnProperty,
        DeleteOwnProperty,
    };

    enum class ModuleGlobalCacheBlocker : uint16_t
    {
        None = 0,
        UncacheableBuiltinsObject = 1 << 0,
        MissingLookupCell = 1 << 1,
    };

    using ModuleGlobalCacheBlockers = uint16_t;

    constexpr ModuleGlobalCacheBlockers
    module_global_cache_blocker(ModuleGlobalCacheBlocker blocker)
    {
        return static_cast<ModuleGlobalCacheBlockers>(blocker);
    }

    class ModuleGlobalReadPlan
    {
    public:
        ModuleObject *storage_owner;
        StorageLocation storage_location;
        ValidityCell *lookup_validity_cell;
        Value builtins_object;
        ModuleGlobalReadPlanKind kind;

        static ModuleGlobalReadPlan slot(ModuleObject *storage_owner,
                                         StorageLocation location,
                                         ValidityCell *lookup_validity_cell)
        {
            return ModuleGlobalReadPlan{storage_owner, location,
                                        lookup_validity_cell, Value::None(),
                                        ModuleGlobalReadPlanKind::Slot};
        }

        static ModuleGlobalReadPlan missing()
        {
            return ModuleGlobalReadPlan{nullptr, StorageLocation::not_found(),
                                        nullptr, Value::None(),
                                        ModuleGlobalReadPlanKind::Missing};
        }

        static ModuleGlobalReadPlan uncacheable_builtins_object(Value object)
        {
            return ModuleGlobalReadPlan{
                nullptr, StorageLocation::not_found(), nullptr, object,
                ModuleGlobalReadPlanKind::UncacheableBuiltinsObject};
        }
    };

    class ModuleGlobalReadDescriptor
    {
    public:
        ModuleGlobalReadStatus status;
        ModuleGlobalReadPlan plan;
        Value lookup_value;
        ModuleGlobalCacheBlockers cache_blockers;

        static ModuleGlobalReadDescriptor not_found()
        {
            return ModuleGlobalReadDescriptor{
                ModuleGlobalReadStatus::NotFound,
                ModuleGlobalReadPlan::missing(), Value::not_present(),
                module_global_cache_blocker(
                    ModuleGlobalCacheBlocker::MissingLookupCell)};
        }

        static ModuleGlobalReadDescriptor
        uncacheable_builtins_object(Value object)
        {
            return ModuleGlobalReadDescriptor{
                ModuleGlobalReadStatus::UncacheableBuiltinsObject,
                ModuleGlobalReadPlan::uncacheable_builtins_object(object),
                Value::not_present(),
                module_global_cache_blocker(
                    ModuleGlobalCacheBlocker::UncacheableBuiltinsObject)};
        }

        static ModuleGlobalReadDescriptor found(ModuleGlobalReadPlan plan,
                                                Value lookup_value)
        {
            return ModuleGlobalReadDescriptor{
                ModuleGlobalReadStatus::Found, plan, lookup_value,
                module_global_cache_blocker(ModuleGlobalCacheBlocker::None)};
        }

        bool is_found() const
        {
            return status == ModuleGlobalReadStatus::Found;
        }

        bool is_cacheable() const
        {
            return plan.lookup_validity_cell != nullptr;
        }
    };

    class ModuleGlobalMutationPlan
    {
    public:
        union
        {
            ModuleObject *storage_owner;
            Shape *next_shape;
        };
        ValidityCell *lookup_validity_cell;
        int32_t physical_idx;
        StorageKind storage_kind;
        ModuleGlobalMutationPlanKind kind;

        static ModuleGlobalMutationPlan
        store_existing(ModuleObject *storage_owner, StorageLocation location,
                       ValidityCell *lookup_validity_cell)
        {
            ModuleGlobalMutationPlan plan;
            plan.storage_owner = storage_owner;
            plan.lookup_validity_cell = lookup_validity_cell;
            plan.physical_idx = location.physical_idx;
            plan.storage_kind = location.kind;
            plan.kind = ModuleGlobalMutationPlanKind::StoreExisting;
            return plan;
        }

        static ModuleGlobalMutationPlan
        add_own_property(Shape *next_shape, StorageLocation location)
        {
            assert(location.kind == StorageKind::Inline);
            ModuleGlobalMutationPlan plan;
            plan.next_shape = next_shape;
            plan.lookup_validity_cell = nullptr;
            plan.physical_idx = location.physical_idx;
            plan.storage_kind = location.kind;
            plan.kind = ModuleGlobalMutationPlanKind::AddOwnProperty;
            return plan;
        }

        static ModuleGlobalMutationPlan
        delete_own_property(Shape *next_shape, StorageLocation location)
        {
            ModuleGlobalMutationPlan plan;
            plan.next_shape = next_shape;
            plan.lookup_validity_cell = nullptr;
            plan.physical_idx = location.physical_idx;
            plan.storage_kind = location.kind;
            plan.kind = ModuleGlobalMutationPlanKind::DeleteOwnProperty;
            return plan;
        }

        StorageLocation storage_location() const
        {
            return StorageLocation{physical_idx, storage_kind};
        }
    };

    class ModuleGlobalWriteDescriptor
    {
    public:
        ModuleGlobalWriteStatus status;
        ModuleGlobalMutationPlan plan;

        static ModuleGlobalWriteDescriptor not_found()
        {
            return ModuleGlobalWriteDescriptor{
                ModuleGlobalWriteStatus::NotFound,
                ModuleGlobalMutationPlan::store_existing(
                    nullptr, StorageLocation::not_found(), nullptr)};
        }

        static ModuleGlobalWriteDescriptor read_only()
        {
            ModuleGlobalWriteDescriptor descriptor = not_found();
            descriptor.status = ModuleGlobalWriteStatus::ReadOnly;
            return descriptor;
        }

        static ModuleGlobalWriteDescriptor disallowed()
        {
            ModuleGlobalWriteDescriptor descriptor = not_found();
            descriptor.status = ModuleGlobalWriteStatus::Disallowed;
            return descriptor;
        }

        static ModuleGlobalWriteDescriptor found(ModuleGlobalMutationPlan plan)
        {
            return ModuleGlobalWriteDescriptor{ModuleGlobalWriteStatus::Found,
                                               plan};
        }

        bool is_found() const
        {
            return status == ModuleGlobalWriteStatus::Found;
        }
        bool is_cacheable() const
        {
            return plan.lookup_validity_cell != nullptr;
        }
    };

    class ModuleGlobalDeleteDescriptor
    {
    public:
        ModuleGlobalDeleteStatus status;
        ModuleGlobalMutationPlan plan;

        static ModuleGlobalDeleteDescriptor not_found()
        {
            return ModuleGlobalDeleteDescriptor{
                ModuleGlobalDeleteStatus::NotFound,
                ModuleGlobalMutationPlan::store_existing(
                    nullptr, StorageLocation::not_found(), nullptr)};
        }

        static ModuleGlobalDeleteDescriptor read_only()
        {
            ModuleGlobalDeleteDescriptor descriptor = not_found();
            descriptor.status = ModuleGlobalDeleteStatus::ReadOnly;
            return descriptor;
        }

        static ModuleGlobalDeleteDescriptor disallowed()
        {
            ModuleGlobalDeleteDescriptor descriptor = not_found();
            descriptor.status = ModuleGlobalDeleteStatus::Disallowed;
            return descriptor;
        }

        static ModuleGlobalDeleteDescriptor found(ModuleGlobalMutationPlan plan)
        {
            return ModuleGlobalDeleteDescriptor{ModuleGlobalDeleteStatus::Found,
                                                plan};
        }

        bool is_found() const
        {
            return status == ModuleGlobalDeleteStatus::Found;
        }
        bool is_cacheable() const
        {
            return plan.lookup_validity_cell != nullptr;
        }
    };

}  // namespace cl

#endif  // CL_MODULE_GLOBAL_DESCRIPTOR_H
