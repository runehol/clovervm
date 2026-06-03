#ifndef CL_VALUE_STRING_H
#define CL_VALUE_STRING_H

#include "object_model/value.h"

namespace cl
{
    [[nodiscard]] Value value_to_repr_string(Value value);
    [[nodiscard]] Value value_to_str_string(Value value);

}  // namespace cl

#endif  // CL_VALUE_STRING_H
