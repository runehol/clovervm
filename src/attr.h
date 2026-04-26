#ifndef CL_ATTR_H
#define CL_ATTR_H

#include "attribute_descriptor.h"
#include "typed_value.h"
#include "value.h"

namespace cl
{
    AttributeReadDescriptor resolve_attr_read_descriptor(Value obj,
                                                         TValue<String> name);
    Value load_attr_from_descriptor(const AttributeReadDescriptor &descriptor);
    bool load_method_from_descriptor(const AttributeReadDescriptor &descriptor,
                                     Value &callable_out, Value &self_out);
    Value load_attr(Value obj, TValue<String> name);
    bool store_attr(Value obj, TValue<String> name, Value value);
    bool load_method(Value obj, TValue<String> name, Value &callable_out,
                     Value &self_out);
}  // namespace cl

#endif  // CL_ATTR_H
