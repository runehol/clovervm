#ifndef CL_ALLOC_H
#define CL_ALLOC_H

#include <stdlib.h>

static inline void *cl_alloc(size_t n_bytes)
{
    return malloc(n_bytes);
}
#define CL_ALLOC_OBJ(obj_type) ((obj_type *)cl_alloc(sizeof(obj_type)))

static inline void cl_free(void *obj)
{
    free(obj);
}

#endif //CL_ALLOC_H

