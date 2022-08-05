#ifndef CJ_SHORT_VECTOR_H
#define CJ_SHORT_VECTOR_H

#include "cj_object.h"
#include "cj_value.h"
#include <stdint.h>
#include <assert.h>

typedef struct cj_short_vector
{
    cj_object obj;
    cj_value count;
    cj_value array[];

} cj_short_vector;

static inline void short_vector_init(cj_short_vector *vec, cj_value count)
{
    assert(value_is_smi(count));
    object_init_all_cells(&vec->obj, NULL, value_get_smi(count));
    vec->count = count;
}


static inline cj_value short_vector_get(const cj_short_vector *vec, cj_value index)
{
    // TODO make these exceptions
    assert(value_is_smi(index) && index.v >= value_make_smi(0).v && index.v < vec->count.v);

    return vec->array[value_get_smi(index)];
}

static inline void short_vector_mutating_set(cj_short_vector *vec, cj_value index, cj_value elem)
{
    // TODO make these exceptions
    assert(value_is_smi(index) && index.v >= 0 && index.v < vec->count.v);

    vec->array[value_get_smi(index)] = elem;
}



#endif //CJ_SHORT_VECTOR_H