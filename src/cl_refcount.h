#ifndef CL_REFCOUNT_H
#define CL_REFCOUNT_H

#include <stdatomic.h>

#include "cl_value.h"
#include "cl_object.h"
#include "cl_alloc.h"


/*
    Refcounting system is borrowed from https://xnning.github.io/papers/perceus.pdf
    Some values are refcounted, others are permanent or interned, and others are inlined.

    When we call a function, we transfer ownership of the argument to that function, 
    and the function is responsible for consuming the argument. Only exceptions are:
    - Function objects being called are borrowed.
    - This arguments are borrowed. In C code, this is indicated by using the actual object type, not cl_value.
    
*/


static inline cl_value cl_incref(cl_value v)
{
    if(value_is_refcounted_ptr(v))
    {
        if(v.ptr->refcount > 0)
        {
            ++v.ptr->refcount;
        } else {
            // atomic refcounts are negative and go the other way
            atomic_fetch_sub_explicit(&v.ptr->refcount, 1, memory_order_relaxed);
        }
    }
    return v;
}

static inline void cl_decref(cl_value v)
{
    if(value_is_refcounted_ptr(v))
    {
        if(v.ptr->refcount > 0)
        {
            if(--v.ptr->refcount == 0)
            {
                cl_free(v.ptr);
            }
        }
    } else {
        if(atomic_fetch_add_explicit(&v.ptr->refcount, 1, memory_order_acq_rel) == -1)
        {
            cl_free(v.ptr);
        }

    }

}

#endif //CL_REFCOUNT_H