#ifndef CL_REFCOUNT_H
#define CL_REFCOUNT_H

#include <stdatomic.h>

#include "cl_value.h"
#include "cl_object.h"
#include "cl_alloc.h"


namespace cl
{




    static inline CLValue cl_incref(CLValue v)
    {
        if(value_is_refcounted_ptr(v))
        {
            ++v.ptr->refcount;
        }
        return v;
    }

    static inline void cl_decref(CLValue v)
    {
        if(value_is_refcounted_ptr(v))
        {
            if(--v.ptr->refcount == 0)
            {
                // todo add to zero count table instead
                cl_free(v.ptr);
            }
        }
    }
}

#endif //CL_REFCOUNT_H
