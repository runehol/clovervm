#ifndef CJ_OBJECT_H
#define CJ_OBJECT_H

#include <stdint.h>

struct cj_class;
/* 
    Base class for all language objects, i.e. indirect values
*/
typedef struct cj_object
{
    struct cj_class *klass;
    uint32_t n_cells;
    uint32_t size_in_cells;
} cj_object;

static inline void object_init(cj_object *obj, struct cj_class *klass, uint32_t n_cells, uint32_t size_in_cells)
{
    obj->klass = klass;
    obj->n_cells = n_cells;
    obj->size_in_cells = size_in_cells;
}


#endif //CJ_OBJECT_H