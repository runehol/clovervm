#ifndef CL_SHAPE_BACKED_OBJECT_H
#define CL_SHAPE_BACKED_OBJECT_H

#include "shape.h"
#include "str.h"
#include "typed_value.h"
#include "value.h"
#include <cassert>

namespace cl::shape_backed_object
{
    enum class StoreOwnPropertyResult : uint8_t
    {
        Stored,
        ReadOnly,
        NotFound,
        AlreadyExists,
    };

    template <typename ObjectT>
    Value get_own_property(const ObjectT *object, TValue<String> name)
    {
        StorageLocation location =
            object->get_shape()->resolve_present_property(name);
        if(!location.is_found())
        {
            return Value::not_present();
        }

        return object->read_storage_location(location);
    }

    template <typename ObjectT>
    StoreOwnPropertyResult
    define_own_property(ObjectT *object, TValue<String> name, Value value,
                        DescriptorFlags descriptor_flags =
                            descriptor_flag(DescriptorFlag::None))
    {
        Shape *current_shape = object->get_shape();
        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx >= 0)
        {
            return StoreOwnPropertyResult::AlreadyExists;
        }

        Shape *next_shape = current_shape->derive_transition(
            name, ShapeTransitionVerb::Add, descriptor_flags);
        object->set_shape(next_shape);

        StorageLocation new_location =
            next_shape->resolve_present_property(name);
        assert(new_location.is_found());
        object->write_storage_location(new_location, value);
        return StoreOwnPropertyResult::Stored;
    }

    template <typename ObjectT>
    StoreOwnPropertyResult
    set_existing_own_property(ObjectT *object, TValue<String> name, Value value)
    {
        Shape *current_shape = object->get_shape();
        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx < 0)
        {
            return StoreOwnPropertyResult::NotFound;
        }

        DescriptorInfo info =
            current_shape->get_descriptor_info(descriptor_idx);
        if(info.has_flag(DescriptorFlag::ReadOnly))
        {
            return StoreOwnPropertyResult::ReadOnly;
        }

        object->write_storage_location(info.storage_location(), value);
        return StoreOwnPropertyResult::Stored;
    }

    template <typename ObjectT>
    StoreOwnPropertyResult set_own_property(ObjectT *object,
                                            TValue<String> name, Value value)
    {
        StoreOwnPropertyResult result =
            set_existing_own_property(object, name, value);
        if(result == StoreOwnPropertyResult::NotFound)
        {
            return define_own_property(object, name, value);
        }
        return result;
    }

    template <typename ObjectT>
    bool delete_own_property(ObjectT *object, TValue<String> name)
    {
        Shape *current_shape = object->get_shape();
        int32_t descriptor_idx = current_shape->lookup_descriptor_index(name);
        if(descriptor_idx < 0)
        {
            return false;
        }

        DescriptorInfo info =
            current_shape->get_descriptor_info(descriptor_idx);
        if(info.has_flag(DescriptorFlag::ReadOnly))
        {
            return false;
        }

        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Delete);
        object->set_shape(next_shape);
        object->write_storage_location(info.storage_location(),
                                       Value::not_present());
        return true;
    }
}  // namespace cl::shape_backed_object

#endif  // CL_SHAPE_BACKED_OBJECT_H
