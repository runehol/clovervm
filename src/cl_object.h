#ifndef CL_OBJECT_H
#define CL_OBJECT_H

#include <stdint.h>
#include <stdatomic.h>

struct cl_klass;
/* 
    Base class for all language objects, i.e. indirect values
*/
typedef struct cl_object
{
    struct cl_klass *klass;
    _Atomic int32_t refcount;
    uint16_t n_cells;
    uint16_t size_in_cells;
} cl_object;



static inline void object_init(cl_object *obj, struct cl_klass *klass, uint32_t n_cells, uint32_t size_in_cells)
{
    obj->klass = klass;
    obj->refcount = 1;
    obj->n_cells = n_cells;
    obj->size_in_cells = size_in_cells;
}

static inline void object_init_all_cells(cl_object *obj, struct cl_klass *klass, uint32_t n_cells)
{
    object_init(obj, klass, n_cells, n_cells);
}

#endif //CL_OBJECT_H