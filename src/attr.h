#ifndef CL_ATTR_H
#define CL_ATTR_H

#include "attribute_descriptor.h"
#include "typed_value.h"
#include "value.h"
#include <cstddef>
#include <cstdint>

namespace cl
{
    class Object;

    struct AttributeMappingEntry
    {
        Value key;
        Value value;
    };

    AttributeReadDescriptor resolve_attr_read_descriptor(Value obj,
                                                         TValue<String> name);
    AttributeReadDescriptor
    resolve_special_method_read_descriptor(Value obj, TValue<String> name);
    Value load_attr_from_plan(Value receiver, const AttributeReadPlan &plan);
    bool load_method_from_plan(Value receiver, const AttributeReadPlan &plan,
                               Value &callable_out, Value &self_out);
    AttributeWriteDescriptor resolve_attr_write_descriptor(Value obj,
                                                           TValue<String> name);
    AttributeDeleteDescriptor
    resolve_attr_delete_descriptor(Value obj, TValue<String> name);
    bool store_attr_from_plan(Value receiver, const AttributeMutationPlan &plan,
                              Value value);
    bool delete_attr_from_plan(Value receiver,
                               const AttributeMutationPlan &plan);
    // Helpers for live slot-backed attribute mappings such as globals() and
    // obj.__dict__. These expose own stored namespace entries only, not normal
    // attribute lookup results from synthetic descriptors, descriptors, or MRO
    // traversal.
    bool descriptor_is_attribute_mapping_entry(DescriptorInfo info);
    bool own_attribute_mapping_entry_at(Object *object, uint32_t descriptor_idx,
                                        AttributeMappingEntry &entry);
    Value load_own_attribute_mapping_entry(Object *object, TValue<String> name);
    size_t count_own_attribute_mapping_entries(Object *object);
    Value load_attr(Value obj, TValue<String> name);
    bool store_attr(Value obj, TValue<String> name, Value value);
    bool delete_attr(Value obj, TValue<String> name);
    bool load_method(Value obj, TValue<String> name, Value &callable_out,
                     Value &self_out);
    bool load_special_method(Value obj, TValue<String> name,
                             Value &callable_out, Value &self_out);
}  // namespace cl

#endif  // CL_ATTR_H
