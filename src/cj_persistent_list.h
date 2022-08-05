#ifndef CJ_PERSISTENT_LIST_H
#define CJ_PERSISTENT_LIST_H

#include "cj_object.h"
#include "cj_value.h"
#include <stdint.h>

typedef struct cj_persistent_list
{
    cj_object obj;
    cj_value first;
    cj_value rest;
    cj_value count;
} cj_persistent_list;

static inline void persistent_list_init(cj_persistent_list *lst, cj_value first, cj_value rest, cj_value count)
{
    object_init_all_cells(&lst->obj, NULL, 3);
    lst->first = first;
    lst->rest = rest;
    lst->count = count;
}

static inline cj_value persistent_list_first(const cj_persistent_list *lst)
{
    return lst->first;
}

static inline cj_value persistent_list_next(const cj_persistent_list *lst)
{
    if(lst->count.v == value_make_smi(1).v) return cj_nil;
    return lst->rest;
}



#endif //CJ_PERSISTENT_LIST_H