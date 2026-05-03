#ifndef CL_SUBSCRIPT_H
#define CL_SUBSCRIPT_H

#include "value.h"

namespace cl
{
    [[nodiscard]] Value load_subscript(Value obj, Value key);
    [[nodiscard]] Value store_subscript(Value obj, Value key, Value value);
    [[nodiscard]] Value del_subscript(Value obj, Value key);
}  // namespace cl

#endif  // CL_SUBSCRIPT_H
