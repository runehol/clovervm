#ifndef CL_VALUE_REPR_H
#define CL_VALUE_REPR_H

#include "value.h"
#include <string>

namespace cl
{
    [[nodiscard]] Value value_repr(Value value);
    [[nodiscard]] Value append_value_repr(std::wstring &out, Value value);

}  // namespace cl

#endif  // CL_VALUE_REPR_H
