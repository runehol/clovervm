#ifndef CL_ATTR_H
#define CL_ATTR_H

#include "attribute_descriptor.h"
#include "typed_value.h"
#include "value.h"

namespace cl
{
    AttributeReadDescriptor resolve_attr_read_descriptor(Value obj,
                                                         TValue<String> name);
    Value load_attr_from_plan(const AttributeReadPlan &plan);
    bool load_method_from_plan(const AttributeReadPlan &plan,
                               Value &callable_out, Value &self_out);
    AttributeWriteDescriptor resolve_attr_write_descriptor(Value obj,
                                                           TValue<String> name);
    bool store_attr_from_plan(const AttributeWritePlan &plan, Value value);
    Value load_attr(Value obj, TValue<String> name);
    bool store_attr(Value obj, TValue<String> name, Value value);
    bool load_method(Value obj, TValue<String> name, Value &callable_out,
                     Value &self_out);
}  // namespace cl

#endif  // CL_ATTR_H
