#ifndef CL_REFCOUNT_H
#define CL_REFCOUNT_H

#include <stdatomic.h>

#include "value.h"
#include "object.h"
#include "alloc.h"


namespace cl
{




    static inline Value cl_incref(Value v)
    {
        if(value_is_refcounted_ptr(v))
        {
            ++v.as.ptr->refcount;
        }
        return v;
    }

    static inline void cl_decref(Value v)
    {
        if(value_is_refcounted_ptr(v))
        {
            if(--v.as.ptr->refcount == 0)
            {
                // todo add to zero count table instead
                cl_free(v.as.ptr);
            }
        }
    }
}

#endif //CL_REFCOUNT_H
