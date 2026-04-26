#ifndef CL_ATTRIBUTE_DESCRIPTOR_H
#define CL_ATTRIBUTE_DESCRIPTOR_H

#include "shape_descriptor.h"
#include "value.h"
#include <cstdint>

namespace cl
{
    class ClassObject;
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

    enum class AttributeReadAccessPath : uint8_t
    {
        ReceiverOwnProperty,
        InstanceClassChain,
        ClassObjectChain,
        MetaclassChain,
    };

    enum class AttributeReadAccessKind : uint8_t
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

    struct AttributeReadAccess
    {
        AttributeReadAccessPath path;
        AttributeReadAccessKind kind;
        const Object *storage_owner;
        StorageLocation storage_location;
        Value value;
        AttributeBindingContext binding;
        ValidityCell *lookup_validity_cell;

        static AttributeReadAccess
        from_storage(AttributeReadAccessPath path, AttributeReadAccessKind kind,
                     const Object *storage_owner, StorageLocation location,
                     Value value, AttributeBindingContext binding,
                     ValidityCell *lookup_validity_cell = nullptr)
        {
            return AttributeReadAccess{
                path,  kind,    storage_owner,       location,
                value, binding, lookup_validity_cell};
        }
    };

    struct AttributeReadDescriptor
    {
        AttributeReadStatus status;
        AttributeReadAccess access;
        AttributeCacheBlockers cache_blockers;

        static AttributeReadDescriptor not_found()
        {
            return AttributeReadDescriptor{
                AttributeReadStatus::NotFound,
                AttributeReadAccess::from_storage(
                    AttributeReadAccessPath::ReceiverOwnProperty,
                    AttributeReadAccessKind::ReturnValue, nullptr,
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
        found(AttributeReadAccess access,
              AttributeCacheBlockers cache_blockers =
                  attribute_cache_blocker(AttributeCacheBlocker::None))
        {
            return AttributeReadDescriptor{AttributeReadStatus::Found, access,
                                           cache_blockers};
        }

        bool is_found() const { return status == AttributeReadStatus::Found; }

        bool is_cacheable() const
        {
            return is_found() && access.lookup_validity_cell != nullptr;
        }
    };

    AttributeReadAccessKind
    attribute_read_access_kind_for_path(AttributeReadAccessPath path,
                                        Value value);
    AttributeCacheBlockers
    attribute_cache_blockers_for_class_value(Value value);
}  // namespace cl

#endif  // CL_ATTRIBUTE_DESCRIPTOR_H
