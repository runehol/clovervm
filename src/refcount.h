#ifndef CL_REFCOUNT_H
#define CL_REFCOUNT_H

#include "value.h"
#include "object.h"


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

    template<typename T>
    T *incref(T *t)
    {
        incref(Value::from_oop(const_cast<typename std::remove_const<T>::type *>(t)));
        return t;
    }

    void add_to_active_zero_count_table(Value v);

    static inline void decref(Value v)
    {
        if(v.is_refcounted_ptr())
        {
            if(--v.as.ptr->refcount == 0)
            {
                add_to_active_zero_count_table(v);
            }
        }
    }
}

#endif //CL_REFCOUNT_H
