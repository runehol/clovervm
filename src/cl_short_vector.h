#ifndef CL_SHORT_VECTOR_H
#define CL_SHORT_VECTOR_H

#include "cl_object.h"
#include "cl_value.h"
#include "cl_refcount.h"
#include <stdint.h>
#include <assert.h>
#include "cl_alloc.h"

namespace cl
{

    struct CLShortVector
    {
        Object obj;
        Value count;
        Value array[];

    };

    extern struct CLKlass cl_short_vector_klass;

    static inline void short_vector_init(CLShortVector *vec, Value count)
    {
        assert(value_is_smi(count));
        object_init_all_cells(&vec->obj, &cl_short_vector_klass, value_get_smi(count));
        vec->count = count;
    }

    static inline Value short_vector_make(Value count)
    {
        CLShortVector *vec = cl_alloc<CLShortVector>(sizeof(CLShortVector)+value_get_smi(count)*sizeof(Value));
        short_vector_init(vec, count);
        return value_make_oop(&vec->obj);
    }


    static inline Value short_vector_get(const CLShortVector *vec, Value index)
    {
        // TODO make these exceptions
        assert(value_is_smi(index) && index.v >= value_make_smi(0).v && index.v < vec->count.v);

        Value v = vec->array[value_get_smi(index)];
        cl_decref(index);
        return cl_incref(v);
    }

    static inline void short_vector_mutating_set(CLShortVector *vec, Value index, Value elem)
    {
        // TODO make these exceptions
        assert(value_is_smi(index) && index.v >= 0 && index.v < vec->count.v);

        vec->array[value_get_smi(index)] = elem;
        cl_decref(index);
    }

}

#endif //CL_SHORT_VECTOR_H
