#ifndef CL_SHORT_VECTOR_H
#define CL_SHORT_VECTOR_H

#include "cl_object.h"
#include "cl_value.h"
#include "cl_refcount.h"
#include <stdint.h>
#include <assert.h>
#include "cl_alloc.h"

typedef struct CLShortVector
{
    CLObject obj;
    CLValue count;
    CLValue array[];

} CLShortVector;

extern struct CLKlass cl_short_vector_klass;

static inline void short_vector_init(CLShortVector *vec, CLValue count)
{
    assert(value_is_smi(count));
    object_init_all_cells(&vec->obj, &cl_short_vector_klass, value_get_smi(count));
    vec->count = count;
}

static inline CLValue short_vector_make(CLValue count)
{
    CLShortVector *vec = CL_ALLOC_OBJ(CLShortVector);
    short_vector_init(vec, count);
    return value_make_oop(&vec->obj);
}


static inline CLValue short_vector_get(const CLShortVector *vec, CLValue index)
{
    // TODO make these exceptions
    assert(value_is_smi(index) && index.v >= value_make_smi(0).v && index.v < vec->count.v);

    CLValue v = vec->array[value_get_smi(index)];
    cl_decref(index);
    return cl_incref(v);
}

static inline void short_vector_mutating_set(CLShortVector *vec, CLValue index, CLValue elem)
{
    // TODO make these exceptions
    assert(value_is_smi(index) && index.v >= 0 && index.v < vec->count.v);

    vec->array[value_get_smi(index)] = elem;
    cl_decref(index);
}



#endif //CL_SHORT_VECTOR_H