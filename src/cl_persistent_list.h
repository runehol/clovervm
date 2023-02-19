#ifndef CL_PERSISTENT_LIST_H
#define CL_PERSISTENT_LIST_H

#include "cl_object.h"
#include "cl_alloc.h"
#include "cl_refcount.h"
#include "cl_value.h"
#include <stdint.h>



typedef struct cl_persistent_list
{
    CLObject obj;
    CLValue first;
    CLValue rest;
    CLValue count;
} cl_persistent_list;

extern struct CLKlass cl_persistent_list_klass;

static inline void persistent_list_init(cl_persistent_list *lst, CLValue first, CLValue rest, CLValue count)
{
    object_init_all_cells(&lst->obj, &cl_persistent_list_klass, 3);
    lst->first = first;
    lst->rest = rest;
    lst->count = count;
}

static inline CLValue persistent_list_make(CLValue first, CLValue rest, CLValue count)
{
    cl_persistent_list *lst = CL_ALLOC_OBJ(cl_persistent_list);
    persistent_list_init(lst, first, rest, count);
    return value_make_oop(&lst->obj);
}


static inline CLValue persistent_list_first(const cl_persistent_list *lst)
{
    return cl_incref(lst->first);
}

static inline CLValue persistent_list_next(const cl_persistent_list *lst)
{
    if(lst->count.v <= value_make_smi(1).v) return cl_None;
    return cl_incref(lst->rest);
}



#endif //CL_PERSISTENT_LIST_H
