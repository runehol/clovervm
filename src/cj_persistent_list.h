#ifndef CJ_persistent_list_H
#define CJ_persistent_list_H

#include "cj_object.h"
#include "cj_value.h"
#include <stdint.h>

typedef struct cj_persistent_list
{
    cj_object obj;
    cj_value first;
    cj_value rest;
    uint32_t count; // TODO or should this be an integer in cj_value encoding?
} cj_persistent_list;

static inline void persistent_list_init(cj_persistent_list *lst, cj_value first, cj_value rest, uint32_t count)
{
    object_init(&lst->obj, NULL, 2, 3);
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
    if(lst->count == 1) return cj_nil;
    return lst->rest;
}



#endif //CJ_persistent_list_H