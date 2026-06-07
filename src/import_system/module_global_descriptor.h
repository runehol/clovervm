#ifndef CL_MODULE_GLOBAL_DESCRIPTOR_H
#define CL_MODULE_GLOBAL_DESCRIPTOR_H

#include "object_model/shape_descriptor.h"
#include "object_model/value.h"
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

    class ModuleGlobalSlotPlan
    {
    public:
        ModuleObject *storage_owner;
        StorageLocation storage_location;

        static ModuleGlobalSlotPlan not_found()
        {
            return ModuleGlobalSlotPlan{nullptr, StorageLocation::not_found()};
        }
    };

    class ModuleGlobalReadPlan
    {
    public:
        ModuleGlobalSlotPlan slot_plan;
        Value builtins_object;
        ModuleGlobalReadPlanKind kind;

        static ModuleGlobalReadPlan slot(ModuleObject *storage_owner,
                                         StorageLocation location)
        {
            return ModuleGlobalReadPlan{
                ModuleGlobalSlotPlan{storage_owner, location}, Value::None(),
                ModuleGlobalReadPlanKind::Slot};
        }

        static ModuleGlobalReadPlan missing()
        {
            return ModuleGlobalReadPlan{ModuleGlobalSlotPlan::not_found(),
                                        Value::None(),
                                        ModuleGlobalReadPlanKind::Missing};
        }

        static ModuleGlobalReadPlan uncacheable_builtins_object(Value object)
        {
            return ModuleGlobalReadPlan{
                ModuleGlobalSlotPlan::not_found(), object,
                ModuleGlobalReadPlanKind::UncacheableBuiltinsObject};
        }
    };

    class ModuleGlobalReadDescriptor
    {
    public:
        ModuleGlobalReadStatus status;
        ModuleGlobalReadPlan plan;
        Value lookup_value;
        ValidityCell *lookup_validity_cell;
        ModuleGlobalCacheBlockers cache_blockers;

        static ModuleGlobalReadDescriptor not_found()
        {
            return ModuleGlobalReadDescriptor{
                ModuleGlobalReadStatus::NotFound,
                ModuleGlobalReadPlan::missing(), Value::not_present(), nullptr,
                module_global_cache_blocker(
                    ModuleGlobalCacheBlocker::MissingLookupCell)};
        }

        static ModuleGlobalReadDescriptor
        uncacheable_builtins_object(Value object)
        {
            return ModuleGlobalReadDescriptor{
                ModuleGlobalReadStatus::UncacheableBuiltinsObject,
                ModuleGlobalReadPlan::uncacheable_builtins_object(object),
                Value::not_present(), nullptr,
                module_global_cache_blocker(
                    ModuleGlobalCacheBlocker::UncacheableBuiltinsObject)};
        }

        static ModuleGlobalReadDescriptor
        found(ModuleGlobalReadPlan plan, Value lookup_value,
              ValidityCell *lookup_validity_cell = nullptr)
        {
            return ModuleGlobalReadDescriptor{
                ModuleGlobalReadStatus::Found, plan, lookup_value,
                lookup_validity_cell,
                module_global_cache_blocker(ModuleGlobalCacheBlocker::None)};
        }

        bool is_found() const
        {
            return status == ModuleGlobalReadStatus::Found;
        }

        bool is_cacheable() const { return lookup_validity_cell != nullptr; }
    };

    class ModuleGlobalStoreExistingPlan
    {
    public:
        ModuleObject *storage_owner;
        int32_t physical_idx;
        StorageKind storage_kind;

        static ModuleGlobalStoreExistingPlan not_found()
        {
            return ModuleGlobalStoreExistingPlan{
                nullptr, StorageLocation::not_found().physical_idx,
                StorageLocation::not_found().kind};
        }

        static ModuleGlobalStoreExistingPlan make(ModuleObject *storage_owner,
                                                  StorageLocation location)
        {
            return ModuleGlobalStoreExistingPlan{
                storage_owner, location.physical_idx, location.kind};
        }

        StorageLocation storage_location() const
        {
            return StorageLocation{physical_idx, storage_kind};
        }
    };

    class ModuleGlobalMutationPlan
    {
    public:
        ModuleGlobalStoreExistingPlan store_existing_plan;
        Shape *next_shape;
        StorageLocation storage_location;
        ModuleGlobalMutationPlanKind kind;

        static ModuleGlobalMutationPlan
        store_existing(ModuleObject *storage_owner, StorageLocation location)
        {
            return ModuleGlobalMutationPlan{
                ModuleGlobalStoreExistingPlan::make(storage_owner, location),
                nullptr, StorageLocation::not_found(),
                ModuleGlobalMutationPlanKind::StoreExisting};
        }

        static ModuleGlobalMutationPlan
        add_own_property(Shape *next_shape, StorageLocation location)
        {
            assert(location.kind == StorageKind::Inline);
            return ModuleGlobalMutationPlan{
                ModuleGlobalStoreExistingPlan::not_found(), next_shape,
                location, ModuleGlobalMutationPlanKind::AddOwnProperty};
        }

        static ModuleGlobalMutationPlan
        delete_own_property(Shape *next_shape, StorageLocation location)
        {
            return ModuleGlobalMutationPlan{
                ModuleGlobalStoreExistingPlan::not_found(), next_shape,
                location, ModuleGlobalMutationPlanKind::DeleteOwnProperty};
        }
    };

    class ModuleGlobalWriteDescriptor
    {
    public:
        ModuleGlobalWriteStatus status;
        ModuleGlobalMutationPlan plan;
        ValidityCell *lookup_validity_cell;

        static ModuleGlobalWriteDescriptor not_found()
        {
            return ModuleGlobalWriteDescriptor{
                ModuleGlobalWriteStatus::NotFound,
                ModuleGlobalMutationPlan::store_existing(
                    nullptr, StorageLocation::not_found()),
                nullptr};
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

        static ModuleGlobalWriteDescriptor
        found(ModuleGlobalMutationPlan plan,
              ValidityCell *lookup_validity_cell = nullptr)
        {
            return ModuleGlobalWriteDescriptor{ModuleGlobalWriteStatus::Found,
                                               plan, lookup_validity_cell};
        }

        bool is_found() const
        {
            return status == ModuleGlobalWriteStatus::Found;
        }
        bool is_cacheable() const { return lookup_validity_cell != nullptr; }
    };

    class ModuleGlobalDeleteDescriptor
    {
    public:
        ModuleGlobalDeleteStatus status;
        ModuleGlobalMutationPlan plan;
        ValidityCell *lookup_validity_cell;

        static ModuleGlobalDeleteDescriptor not_found()
        {
            return ModuleGlobalDeleteDescriptor{
                ModuleGlobalDeleteStatus::NotFound,
                ModuleGlobalMutationPlan::store_existing(
                    nullptr, StorageLocation::not_found()),
                nullptr};
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

        static ModuleGlobalDeleteDescriptor
        found(ModuleGlobalMutationPlan plan,
              ValidityCell *lookup_validity_cell = nullptr)
        {
            return ModuleGlobalDeleteDescriptor{ModuleGlobalDeleteStatus::Found,
                                                plan, lookup_validity_cell};
        }

        bool is_found() const
        {
            return status == ModuleGlobalDeleteStatus::Found;
        }
        bool is_cacheable() const { return lookup_validity_cell != nullptr; }
    };

}  // namespace cl

#endif  // CL_MODULE_GLOBAL_DESCRIPTOR_H
