#ifndef CL_SHAPE_BACKED_OBJECT_H
#define CL_SHAPE_BACKED_OBJECT_H

#include "shape.h"
#include "str.h"
#include "typed_value.h"
#include "value.h"
#include <cassert>

namespace cl::shape_backed_object
{
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
    void set_own_property(ObjectT *object, TValue<String> name, Value value)
    {
        Shape *current_shape = object->get_shape();
        StorageLocation location =
            current_shape->resolve_present_property(name);
        if(location.is_found())
        {
            object->write_storage_location(location, value);
            return;
        }

        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Add);
        object->set_shape(next_shape);

        StorageLocation new_location =
            next_shape->resolve_present_property(name);
        assert(new_location.is_found());
        object->write_storage_location(new_location, value);
    }

    template <typename ObjectT>
    bool delete_own_property(ObjectT *object, TValue<String> name)
    {
        Shape *current_shape = object->get_shape();
        StorageLocation location =
            current_shape->resolve_present_property(name);
        if(!location.is_found())
        {
            return false;
        }

        Shape *next_shape =
            current_shape->derive_transition(name, ShapeTransitionVerb::Delete);
        object->set_shape(next_shape);
        object->write_storage_location(location, Value::not_present());
        return true;
    }
}  // namespace cl::shape_backed_object

#endif  // CL_SHAPE_BACKED_OBJECT_H
