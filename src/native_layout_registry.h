#ifndef CL_NATIVE_LAYOUT_REGISTRY_H
#define CL_NATIVE_LAYOUT_REGISTRY_H

#include "dict.h"
#include "exception_object.h"
#include "function.h"
#include "list.h"
#include "list_iterator.h"
#include "range_iterator.h"
#include "tuple_iterator.h"

#define CL_NATIVE_LAYOUT_REGISTRY(V)                                           \
    V(List);                                                                   \
    V(RangeIterator);                                                          \
    V(TupleIterator);                                                          \
    V(ListIterator);                                                           \
    V(ExceptionObject);                                                        \
    V(StopIterationObject);                                                    \
    V(Function);                                                               \
    V(Dict);

#endif  // CL_NATIVE_LAYOUT_REGISTRY_H
