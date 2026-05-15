#ifndef CL_NATIVE_LAYOUT_REGISTRY_H
#define CL_NATIVE_LAYOUT_REGISTRY_H

#include "class_object.h"
#include "code_object.h"
#include "dict.h"
#include "exception_object.h"
#include "function.h"
#include "instance.h"
#include "list.h"
#include "list_iterator.h"
#include "overflow_slots.h"
#include "range_iterator.h"
#include "scope.h"
#include "shape.h"
#include "str.h"
#include "tuple.h"
#include "tuple_iterator.h"
#include "validity_cell.h"

#define CL_NATIVE_LAYOUT_REGISTRY(V)                                           \
    V(List);                                                                   \
    V(Tuple);                                                                  \
    V(RangeIterator);                                                          \
    V(TupleIterator);                                                          \
    V(ListIterator);                                                           \
    V(ExceptionObject);                                                        \
    V(StopIterationObject);                                                    \
    V(Function);                                                               \
    V(Dict);                                                                   \
    V(String);                                                                 \
    V(Instance);                                                               \
    V(CodeObject);                                                             \
    V(ValidityCell);                                                           \
    V(Scope);                                                                  \
    V(Shape);                                                                  \
    V(OverflowSlots);                                                          \
    V(ClassObject);

#endif  // CL_NATIVE_LAYOUT_REGISTRY_H
