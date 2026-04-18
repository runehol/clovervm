#ifndef CL_ATTR_H
#define CL_ATTR_H

#include "typed_value.h"
#include "value.h"

namespace cl
{
    Value load_attr(Value obj, TValue<String> name);
    bool store_attr(Value obj, TValue<String> name, Value value);
    bool load_method(Value obj, TValue<String> name, Value &callable_out,
                     Value &self_out);
}  // namespace cl

#endif  // CL_ATTR_H
