#ifndef CL_REFCOUNT_H
#define CL_REFCOUNT_H

#include "object.h"
#include "value.h"

namespace cl
{

    static inline Value incref_refcounted_ptr(Value v)
    {
        assert(v.is_refcounted_ptr());
        ++v.as.ptr->refcount;
        return v;
    }

    static inline bool heap_ptr_is_refcounted(HeapObject *obj)
    {
        return (reinterpret_cast<uintptr_t>(obj) & value_ptr_mask) ==
               value_refcounted_ptr_tag;
    }

    static inline bool heap_ptr_is_interned(HeapObject *obj)
    {
        return (reinterpret_cast<uintptr_t>(obj) & value_ptr_mask) ==
               value_interned_ptr_tag;
    }

    static inline HeapObject *incref_heap_ptr(HeapObject *obj)
    {
        assert(obj == nullptr || heap_ptr_is_refcounted(obj) ||
               heap_ptr_is_interned(obj));
        if(heap_ptr_is_refcounted(obj))
        {
            ++obj->refcount;
        }
        return obj;
    }

    static inline Value incref(Value v)
    {
        if(v.is_refcounted_ptr())
        {
            ++v.as.ptr->refcount;
        }
        return v;
    }

    template <typename T> T *incref(T *t)
    {
        HeapObject *obj = const_cast<typename std::remove_const<T>::type *>(t);
        incref_heap_ptr(obj);
        return t;
    }

    void add_to_active_zero_count_table(HeapObject *obj);

    static inline void decref_refcounted_ptr(Value v)
    {
        assert(v.is_refcounted_ptr());
        if(--v.as.ptr->refcount == 0)
        {
            add_to_active_zero_count_table(v.as.ptr);
        }
    }

    static inline void decref_heap_ptr(HeapObject *obj)
    {
        assert(obj == nullptr || heap_ptr_is_refcounted(obj) ||
               heap_ptr_is_interned(obj));
        if(heap_ptr_is_refcounted(obj) && --obj->refcount == 0)
        {
            add_to_active_zero_count_table(obj);
        }
    }

    static inline void decref(Value v)
    {
        if(v.is_refcounted_ptr())
        {
            if(--v.as.ptr->refcount == 0)
            {
                add_to_active_zero_count_table(v.as.ptr);
            }
        }
    }

    template <typename T> T *decref(T *t)
    {
        HeapObject *obj = const_cast<typename std::remove_const<T>::type *>(t);
        decref_heap_ptr(obj);
        return t;
    }

}  // namespace cl

#endif  // CL_REFCOUNT_H
