#ifndef CL_SUBSCRIPT_H
#define CL_SUBSCRIPT_H

#include "value.h"

namespace cl
{
    Value load_subscript(Value obj, Value key);
    bool store_subscript(Value obj, Value key, Value value);
}  // namespace cl

#endif  // CL_SUBSCRIPT_H
