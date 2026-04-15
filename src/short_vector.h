#ifndef CL_SHORT_VECTOR_H
#define CL_SHORT_VECTOR_H

#include "object.h"
#include "refcount.h"
#include "value.h"
#include <assert.h>
#include <stdint.h>

namespace cl
{

    // Historical leftover from an earlier Clojure-runtime direction where this
    // was intended to model a persistent vector. It is currently orphaned and
    // should not be treated as the preferred substrate for new VM container or
    // slot-array work.
    extern struct Klass cl_short_vector_klass;

    struct CLShortVector : public Object
    {
        CLShortVector(Value count)
            : Object(&cl_short_vector_klass), count(count)
        {
        }
        Value count;
        Value array[];

        static size_t size_for(Value count)
        {
            return sizeof(CLShortVector) + count.get_smi() * sizeof(Value);
        }

        static DynamicLayoutSpec layout_spec_for(Value count)
        {
            return DynamicLayoutSpec{round_up_to_16byte_units(size_for(count)),
                                     uint64_t(count.get_smi() + 1)};
        }

        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(CLShortVector, count);
    };

    static inline Value short_vector_get(const CLShortVector *vec, Value index)
    {
        // TODO make these exceptions
        assert(index.is_smi() &&
               index.as.integer >= Value::from_smi(0).as.integer &&
               index.as.integer < vec->count.as.integer);

        Value v = vec->array[index.get_smi()];
        return v;
    }

    static inline void short_vector_mutating_set(CLShortVector *vec,
                                                 Value index, Value elem)
    {
        // TODO make these exceptions
        assert(index.is_smi() && index.as.integer >= 0 &&
               index.as.integer < vec->count.as.integer);

        int64_t idx = index.get_smi();

        // make sure we incref the new value before decrefing the old value, in
        // case we're assigning the same value
        Value old_val = vec->array[idx];
        vec->array[idx] = incref(elem);
        decref(old_val);
    }

}  // namespace cl

#endif  // CL_SHORT_VECTOR_H
