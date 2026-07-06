#ifndef CL_EXCEPTION_PROPAGATION_H
#define CL_EXCEPTION_PROPAGATION_H

#include "object_model/value.h"

#define CL_PROPAGATE_EXCEPTION(expr)                                           \
    do                                                                         \
    {                                                                          \
        ::cl::Value cl_exception_propagation_result = (expr);                  \
        if(unlikely(cl_exception_propagation_result.is_exception_marker()))    \
        {                                                                      \
            return cl_exception_propagation_result;                            \
        }                                                                      \
    }                                                                          \
    while(false)

#define CL_SWALLOW_EXCEPTION(thread, expr)                                     \
    do                                                                         \
    {                                                                          \
        auto cl_exception_swallow_result = (expr);                             \
        if(unlikely(!cl_exception_swallow_result))                             \
        {                                                                      \
            (thread)->clear_pending_exception();                               \
        }                                                                      \
    }                                                                          \
    while(false)

#endif  // CL_EXCEPTION_PROPAGATION_H
