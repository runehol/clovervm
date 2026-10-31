#ifndef CL_REFCOUNT_H
#define CL_REFCOUNT_H

#include "object_model/heap_ptr.h"
#include "object_model/value.h"

namespace cl
{

    static inline Value incref_refcounted_ptr(Value v)
    {
        assert(v.is_refcounted_ptr());
        ++v.as.ptr->refcount();
        return v;
    }

    static inline Value incref(Value v)
    {
        if(v.is_refcounted_ptr())
        {
            ++v.as.ptr->refcount();
        }
        return v;
    }

    static inline void decref_refcounted_ptr(Value v)
    {
        assert(v.is_refcounted_ptr());
        if(--v.as.ptr->refcount() == 0)
        {
            add_to_active_zero_count_table_if_needed(v.as.ptr);
        }
    }

    static inline void decref(Value v)
    {
        if(v.is_refcounted_ptr())
        {
            if(--v.as.ptr->refcount() == 0)
            {
                add_to_active_zero_count_table_if_needed(v.as.ptr);
            }
        }
    }

}  // namespace cl

#endif  // CL_REFCOUNT_H
