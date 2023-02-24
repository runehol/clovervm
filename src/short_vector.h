#ifndef CL_SHORT_VECTOR_H
#define CL_SHORT_VECTOR_H

#include "object.h"
#include "value.h"
#include "refcount.h"
#include <stdint.h>
#include <assert.h>
#include "alloc.h"

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
        assert(count.is_smi());
        object_init_all_cells(&vec->obj, &cl_short_vector_klass, count.get_smi());
        vec->count = count;
    }

    static inline Value short_vector_make(Value count)
    {
        CLShortVector *vec = cl_alloc<CLShortVector>(sizeof(CLShortVector)+count.get_smi()*sizeof(Value));
        short_vector_init(vec, count);
        return Value::from_oop(&vec->obj);
    }


    static inline Value short_vector_get(const CLShortVector *vec, Value index)
    {
        // TODO make these exceptions
        assert(index.is_smi() && index.as.integer >= Value::from_smi(0).as.integer && index.as.integer < vec->count.as.integer);

        Value v = vec->array[index.get_smi()];
        return v;
    }

    static inline void short_vector_mutating_set(CLShortVector *vec, Value index, Value elem)
    {
        // TODO make these exceptions
        assert(index.is_smi() && index.as.integer >= 0 && index.as.integer < vec->count.as.integer);

        int64_t idx = index.get_smi();

        // make sure we incref the new value before decrefing the old value, in case we're assigning the same value
        Value old_val = vec->array[idx];
        vec->array[idx] = incref(elem);
        decref(old_val);
    }

}

#endif //CL_SHORT_VECTOR_H
