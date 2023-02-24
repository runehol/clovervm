#ifndef CL_ALLOC_H
#define CL_ALLOC_H

#include <cstdlib>

namespace cl
{

    template<typename T>
    static inline T *cl_alloc(size_t n_bytes)
    {
        return (T *)malloc(n_bytes);
    }

    template<typename T>
    static inline T *cl_alloc()
    {
        return cl_alloc<T>(sizeof(T));
    }

    static inline void cl_free(void *obj)
    {
        free(obj);
    }
}

#endif //CL_ALLOC_H
