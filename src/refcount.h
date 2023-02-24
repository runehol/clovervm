#ifndef CL_REFCOUNT_H
#define CL_REFCOUNT_H

#include "value.h"
#include "object.h"
#include "alloc.h"
#include "thread_state.h"


namespace cl
{




    static inline Value incref(Value v)
    {
        if(v.is_refcounted_ptr())
        {
            ++v.as.ptr->refcount;
        }
        return v;
    }

    static inline void decref(Value v)
    {
        if(v.is_refcounted_ptr())
        {
            if(--v.as.ptr->refcount == 0)
            {
                ThreadState::add_to_active_zero_count_table(v);
            }
        }
    }
}

#endif //CL_REFCOUNT_H
