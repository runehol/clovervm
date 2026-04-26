#ifndef CL_ATTRIBUTE_DESCRIPTOR_H
#define CL_ATTRIBUTE_DESCRIPTOR_H

#include "shape_descriptor.h"
#include "value.h"
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

    enum class AttributeReadPlanPath : uint8_t
    {
        ReceiverOwnProperty,
        InstanceClassChain,
        ClassObjectChain,
        MetaclassChain,
    };

    enum class AttributeReadPlanKind : uint8_t
    {
        ReturnValue,
        ReceiverSlot,
        ResolvedValue,
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
        Value value;
        AttributeBindingContext binding;
        ValidityCell *lookup_validity_cell;

        static AttributeReadPlan
        from_storage(AttributeReadPlanPath path, AttributeReadPlanKind kind,
                     const Object *storage_owner, StorageLocation location,
                     Value value, AttributeBindingContext binding,
                     ValidityCell *lookup_validity_cell = nullptr)
        {
            return AttributeReadPlan{
                path,  kind,    storage_owner,       location,
                value, binding, lookup_validity_cell};
        }
    };

    struct AttributeReadDescriptor
    {
        AttributeReadStatus status;
        AttributeReadPlan plan;
        AttributeCacheBlockers cache_blockers;

        static AttributeReadDescriptor not_found()
        {
            return AttributeReadDescriptor{
                AttributeReadStatus::NotFound,
                AttributeReadPlan::from_storage(
                    AttributeReadPlanPath::ReceiverOwnProperty,
                    AttributeReadPlanKind::ReturnValue, nullptr,
                    StorageLocation::not_found(), Value::not_present(),
                    AttributeBindingContext::none()),
                attribute_cache_blocker(AttributeCacheBlocker::None)};
        }

        static AttributeReadDescriptor non_object_receiver()
        {
            AttributeReadDescriptor descriptor = not_found();
            descriptor.status = AttributeReadStatus::NonObjectReceiver;
            return descriptor;
        }

        static AttributeReadDescriptor
        found(AttributeReadPlan plan,
              AttributeCacheBlockers cache_blockers =
                  attribute_cache_blocker(AttributeCacheBlocker::None))
        {
            return AttributeReadDescriptor{AttributeReadStatus::Found, plan,
                                           cache_blockers};
        }

        bool is_found() const { return status == AttributeReadStatus::Found; }

        bool is_cacheable() const
        {
            return is_found() && plan.lookup_validity_cell != nullptr &&
                   plan.kind != AttributeReadPlanKind::DataDescriptorGet &&
                   plan.kind != AttributeReadPlanKind::NonDataDescriptorGet;
        }
    };

    struct AttributeWritePlan
    {
        Object *storage_owner;
        StorageLocation storage_location;
        ValidityCell *lookup_validity_cell;

        static AttributeWritePlan
        store_existing(Object *storage_owner, StorageLocation location,
                       ValidityCell *lookup_validity_cell)
        {
            return AttributeWritePlan{storage_owner, location,
                                      lookup_validity_cell};
        }
    };

    struct AttributeWriteDescriptor
    {
        AttributeWriteStatus status;
        AttributeWritePlan plan;

        static AttributeWriteDescriptor not_found()
        {
            return AttributeWriteDescriptor{
                AttributeWriteStatus::NotFound,
                AttributeWritePlan::store_existing(
                    nullptr, StorageLocation::not_found(), nullptr)};
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

        static AttributeWriteDescriptor found(AttributeWritePlan plan)
        {
            return AttributeWriteDescriptor{AttributeWriteStatus::Found, plan};
        }

        bool is_found() const { return status == AttributeWriteStatus::Found; }
        bool is_cacheable() const
        {
            return is_found() && plan.lookup_validity_cell != nullptr;
        }
    };

    AttributeReadPlanKind
    attribute_read_plan_kind_for_path(AttributeReadPlanPath path, Value value);
    AttributeCacheBlockers
    attribute_cache_blockers_for_class_value(Value value);
}  // namespace cl

#endif  // CL_ATTRIBUTE_DESCRIPTOR_H
