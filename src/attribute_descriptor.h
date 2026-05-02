#ifndef CL_ATTRIBUTE_DESCRIPTOR_H
#define CL_ATTRIBUTE_DESCRIPTOR_H

#include "shape_descriptor.h"
#include "value.h"
#include <cassert>
#include <cstdint>

namespace cl
{
    class ClassObject;
    class Shape;
    class ValidityCell;
    struct Object;

    enum class AttributeReadStatus : uint8_t
    {
        Found = 0,
        NotFound,
        NonObjectReceiver,
        Error,
    };

    static_assert(static_cast<uint8_t>(AttributeReadStatus::Found) == 0);

    enum class AttributeWriteStatus : uint8_t
    {
        Found = 0,
        NotFound,
        AlreadyExists,
        ReadOnly,
        Disallowed,
        NonObjectReceiver,
        Error,
    };

    static_assert(static_cast<uint8_t>(AttributeWriteStatus::Found) == 0);

    enum class AttributeDeleteStatus : uint8_t
    {
        Found = 0,
        NotFound,
        ReadOnly,
        Disallowed,
        NonObjectReceiver,
        Error,
    };

    static_assert(static_cast<uint8_t>(AttributeDeleteStatus::Found) == 0);

    enum class AttributeMutationPlanKind : uint8_t
    {
        StoreExisting,
        AddOwnProperty,
        DeleteOwnProperty,
    };

    enum class AttributeReadPlanPath : uint8_t
    {
        ReceiverOwnProperty,
        InstanceClassChain,
        ClassObjectChain,
        MetaclassChain,
    };

    enum class AttributeReadPlanKind : uint8_t
    {
        ReceiverSlot,
        BindFunctionReceiver,
        DataDescriptorGet,
        NonDataDescriptorGet,
    };

    enum class AttributeCacheBlocker : uint16_t
    {
        None = 0,
        MutableDescriptorType = 1 << 0,
        CustomGetAttribute = 1 << 1,
        MissingLookupCell = 1 << 2,
        UnsupportedDescriptorKind = 1 << 3,
    };

    using AttributeCacheBlockers = uint16_t;

    constexpr AttributeCacheBlockers
    attribute_cache_blocker(AttributeCacheBlocker blocker)
    {
        return static_cast<AttributeCacheBlockers>(blocker);
    }

    constexpr AttributeCacheBlockers
    attribute_cache_blockers(AttributeCacheBlockers left,
                             AttributeCacheBlocker right)
    {
        return left | attribute_cache_blocker(right);
    }

    constexpr bool
    attribute_cache_blockers_are_none(AttributeCacheBlockers blockers)
    {
        return blockers == attribute_cache_blocker(AttributeCacheBlocker::None);
    }

    struct AttributeBindingContext
    {
        Value self;
        const ClassObject *owner;

        static AttributeBindingContext none()
        {
            return AttributeBindingContext{Value::None(), nullptr};
        }
    };

    struct AttributeReadPlan
    {
        AttributeReadPlanPath path;
        AttributeReadPlanKind kind;
        const Object *storage_owner;
        StorageLocation storage_location;
        AttributeBindingContext binding;
        ValidityCell *lookup_validity_cell;

        static AttributeReadPlan
        from_storage(AttributeReadPlanPath path, AttributeReadPlanKind kind,
                     const Object *storage_owner, StorageLocation location,
                     AttributeBindingContext binding,
                     ValidityCell *lookup_validity_cell = nullptr)
        {
            return AttributeReadPlan{path,     kind,    storage_owner,
                                     location, binding, lookup_validity_cell};
        }
    };

    struct AttributeReadDescriptor
    {
        AttributeReadStatus status;
        AttributeReadPlan plan;
        Value lookup_value;
        AttributeCacheBlockers cache_blockers;

        static AttributeReadDescriptor not_found()
        {
            return AttributeReadDescriptor{
                AttributeReadStatus::NotFound,
                AttributeReadPlan::from_storage(
                    AttributeReadPlanPath::ReceiverOwnProperty,
                    AttributeReadPlanKind::ReceiverSlot, nullptr,
                    StorageLocation::not_found(),
                    AttributeBindingContext::none()),
                Value::not_present(),
                attribute_cache_blocker(AttributeCacheBlocker::None)};
        }

        static AttributeReadDescriptor non_object_receiver()
        {
            AttributeReadDescriptor descriptor = not_found();
            descriptor.status = AttributeReadStatus::NonObjectReceiver;
            return descriptor;
        }

        static AttributeReadDescriptor
        found(AttributeReadPlan plan, Value lookup_value,
              AttributeCacheBlockers cache_blockers =
                  attribute_cache_blocker(AttributeCacheBlocker::None))
        {
            return AttributeReadDescriptor{AttributeReadStatus::Found, plan,
                                           lookup_value, cache_blockers};
        }

        bool is_found() const { return status == AttributeReadStatus::Found; }

        bool is_cacheable() const
        {
            return plan.lookup_validity_cell != nullptr;
        }
    };

    struct AttributeMutationPlan
    {
        union
        {
            Object *storage_owner;
            Shape *next_shape;
        };
        ValidityCell *lookup_validity_cell;
        int32_t physical_idx;
        StorageKind storage_kind;
        AttributeMutationPlanKind kind;

        static AttributeMutationPlan
        store_existing(Object *storage_owner, StorageLocation location,
                       ValidityCell *lookup_validity_cell)
        {
            AttributeMutationPlan plan;
            plan.storage_owner = storage_owner;
            plan.lookup_validity_cell = lookup_validity_cell;
            plan.physical_idx = location.physical_idx;
            plan.storage_kind = location.kind;
            plan.kind = AttributeMutationPlanKind::StoreExisting;
            return plan;
        }

        static AttributeMutationPlan
        add_own_property(Shape *next_shape, StorageLocation location,
                         ValidityCell *lookup_validity_cell)
        {
            assert(location.kind == StorageKind::Inline);
            AttributeMutationPlan plan;
            plan.next_shape = next_shape;
            plan.lookup_validity_cell = lookup_validity_cell;
            plan.physical_idx = location.physical_idx;
            plan.storage_kind = location.kind;
            plan.kind = AttributeMutationPlanKind::AddOwnProperty;
            return plan;
        }

        static AttributeMutationPlan
        delete_own_property(Shape *next_shape, StorageLocation location,
                            ValidityCell *lookup_validity_cell)
        {
            AttributeMutationPlan plan;
            plan.next_shape = next_shape;
            plan.lookup_validity_cell = lookup_validity_cell;
            plan.physical_idx = location.physical_idx;
            plan.storage_kind = location.kind;
            plan.kind = AttributeMutationPlanKind::DeleteOwnProperty;
            return plan;
        }

        StorageLocation storage_location() const
        {
            return StorageLocation{physical_idx, storage_kind};
        }

        bool is_add_own_property() const
        {
            return kind == AttributeMutationPlanKind::AddOwnProperty;
        }

        bool is_delete_own_property() const
        {
            return kind == AttributeMutationPlanKind::DeleteOwnProperty;
        }
    };

    static_assert(
        sizeof(AttributeMutationPlan) == 24,
        "AttributeMutationPlan should stay compact for mutation caches");

    struct AttributeWriteDescriptor
    {
        AttributeWriteStatus status;
        AttributeMutationPlan plan;
        AttributeCacheBlockers cache_blockers;

        static AttributeWriteDescriptor not_found()
        {
            return AttributeWriteDescriptor{
                AttributeWriteStatus::NotFound,
                AttributeMutationPlan::store_existing(
                    nullptr, StorageLocation::not_found(), nullptr),
                attribute_cache_blocker(AttributeCacheBlocker::None)};
        }

        static AttributeWriteDescriptor already_exists()
        {
            AttributeWriteDescriptor descriptor = not_found();
            descriptor.status = AttributeWriteStatus::AlreadyExists;
            return descriptor;
        }

        static AttributeWriteDescriptor read_only()
        {
            AttributeWriteDescriptor descriptor = not_found();
            descriptor.status = AttributeWriteStatus::ReadOnly;
            return descriptor;
        }

        static AttributeWriteDescriptor disallowed()
        {
            AttributeWriteDescriptor descriptor = not_found();
            descriptor.status = AttributeWriteStatus::Disallowed;
            return descriptor;
        }

        static AttributeWriteDescriptor non_object_receiver()
        {
            AttributeWriteDescriptor descriptor = not_found();
            descriptor.status = AttributeWriteStatus::NonObjectReceiver;
            return descriptor;
        }

        static AttributeWriteDescriptor found(AttributeMutationPlan plan)
        {
            return AttributeWriteDescriptor{
                AttributeWriteStatus::Found, plan,
                attribute_cache_blocker(AttributeCacheBlocker::None)};
        }

        bool is_found() const { return status == AttributeWriteStatus::Found; }
        bool is_cacheable() const
        {
            return plan.lookup_validity_cell != nullptr;
        }
    };

    struct AttributeDeleteDescriptor
    {
        AttributeDeleteStatus status;
        AttributeMutationPlan plan;
        AttributeCacheBlockers cache_blockers;

        static AttributeDeleteDescriptor not_found()
        {
            return AttributeDeleteDescriptor{
                AttributeDeleteStatus::NotFound,
                AttributeMutationPlan::store_existing(
                    nullptr, StorageLocation::not_found(), nullptr),
                attribute_cache_blocker(AttributeCacheBlocker::None)};
        }

        static AttributeDeleteDescriptor read_only()
        {
            AttributeDeleteDescriptor descriptor = not_found();
            descriptor.status = AttributeDeleteStatus::ReadOnly;
            return descriptor;
        }

        static AttributeDeleteDescriptor disallowed()
        {
            AttributeDeleteDescriptor descriptor = not_found();
            descriptor.status = AttributeDeleteStatus::Disallowed;
            return descriptor;
        }

        static AttributeDeleteDescriptor non_object_receiver()
        {
            AttributeDeleteDescriptor descriptor = not_found();
            descriptor.status = AttributeDeleteStatus::NonObjectReceiver;
            return descriptor;
        }

        static AttributeDeleteDescriptor found(AttributeMutationPlan plan)
        {
            return AttributeDeleteDescriptor{
                AttributeDeleteStatus::Found, plan,
                attribute_cache_blocker(AttributeCacheBlocker::None)};
        }

        bool is_found() const { return status == AttributeDeleteStatus::Found; }
        bool is_cacheable() const
        {
            return plan.lookup_validity_cell != nullptr;
        }
    };

}  // namespace cl

#endif  // CL_ATTRIBUTE_DESCRIPTOR_H
