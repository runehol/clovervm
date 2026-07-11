#ifndef CL_NATIVE_LAYOUT_REGISTRY_H
#define CL_NATIVE_LAYOUT_REGISTRY_H

#include "api/extension_handle.h"
#include "builtin_types/bigint.h"
#include "builtin_types/dict.h"
#include "builtin_types/dict_view.h"
#include "builtin_types/float.h"
#include "builtin_types/list.h"
#include "builtin_types/list_iterator.h"
#include "builtin_types/module_loader_object.h"
#include "builtin_types/module_object.h"
#include "builtin_types/module_spec_object.h"
#include "builtin_types/range_iterator.h"
#include "builtin_types/slice.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "builtin_types/tuple_iterator.h"
#include "bytecode/code_object.h"
#include "compiler/scope.h"
#include "object_model/class_object.h"
#include "object_model/function.h"
#include "object_model/instance.h"
#include "object_model/overflow_slots.h"
#include "object_model/shape.h"
#include "object_model/slot_dict.h"
#include "object_model/validity_cell.h"
#include "object_model/vm_array_backing.h"
#include "runtime/exception_object.h"

#define CL_NATIVE_LAYOUT_REGISTRY(V)                                           \
    V(BigInt);                                                                 \
    V(List);                                                                   \
    V(Tuple);                                                                  \
    V(RangeIterator);                                                          \
    V(TupleIterator);                                                          \
    V(ListIterator);                                                           \
    V(DictKeysView);                                                           \
    V(DictValuesView);                                                         \
    V(DictItemsView);                                                          \
    V(DictKeyIterator);                                                        \
    V(DictValueIterator);                                                      \
    V(DictItemIterator);                                                       \
    V(ModuleObject);                                                           \
    V(ModuleLoaderObject);                                                     \
    V(ModuleSpecObject);                                                       \
    V(ExceptionObject);                                                        \
    V(StopIterationObject);                                                    \
    V(Float);                                                                  \
    V(Function);                                                               \
    V(Dict);                                                                   \
    V(SlotDict);                                                               \
    V(Slice);                                                                  \
    V(String);                                                                 \
    V(Instance);                                                               \
    V(CodeObject);                                                             \
    V(ValidityCell);                                                           \
    V(Scope);                                                                  \
    V(Shape);                                                                  \
    V(OverflowSlots);                                                          \
    V(RawArrayBacking);                                                        \
    V(ValueArrayBacking);                                                      \
    V(HeapPtrArrayBacking);                                                    \
    V(HandleChunk);                                                            \
    V(ClassObject);

#endif  // CL_NATIVE_LAYOUT_REGISTRY_H
