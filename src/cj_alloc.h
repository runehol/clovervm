#ifndef CJ_ALLOC_H
#define CJ_ALLOC_H

#include <stdlib.h>

void *cj_alloc(size_t n_bytes)
{
    return malloc(n_bytes);
}
#define CJ_ALLOC_OBJ(obj_type) cj_alloc(sizeof(obj_type))

void cj_free(void *obj)
{
    free(obj);
}

#endif //CJ_ALLOC_H

