#ifndef CL_ATTRIBUTE_CACHE_H
#define CL_ATTRIBUTE_CACHE_H

#include "object_model/attribute_descriptor.h"
#include "object_model/object.h"
#include "object_model/shape.h"
#include "object_model/validity_cell.h"
#include "object_model/value.h"
#include <cassert>

namespace cl
{
    class AttributeReadInlineCache
    {
    public:
        Shape *receiver_shape = nullptr;
        AttributeReadPlan plan = AttributeReadDescriptor::not_found().plan;

        ALWAYSINLINE bool matches(Value receiver) const
        {
            return receiver.is_ptr() && receiver_shape != nullptr &&
                   receiver.get_ptr<Object>()->get_shape() == receiver_shape &&
                   plan.lookup_validity_cell != nullptr &&
                   plan.lookup_validity_cell->is_valid();
        }

        void populate(Value receiver, const AttributeReadDescriptor &descriptor)
        {
            assert(receiver.is_ptr());
            assert(descriptor.is_cacheable());
            receiver_shape = receiver.get_ptr<Object>()->get_shape();
            plan = descriptor.plan;
        }

        void clear()
        {
            receiver_shape = nullptr;
            plan = AttributeReadDescriptor::not_found().plan;
        }
    };

    class AttributeMutationInlineCache
    {
    public:
        Shape *receiver_shape = nullptr;
        AttributeMutationPlan plan = AttributeWriteDescriptor::not_found().plan;

        ALWAYSINLINE bool matches(Value receiver) const
        {
            return receiver.is_ptr() && receiver_shape != nullptr &&
                   receiver.get_ptr<Object>()->get_shape() == receiver_shape &&
                   plan.lookup_validity_cell != nullptr &&
                   plan.lookup_validity_cell->is_valid();
        }

        void populate(Value receiver,
                      const AttributeWriteDescriptor &descriptor)
        {
            assert(receiver.is_ptr());
            assert(descriptor.is_cacheable());
            receiver_shape = receiver.get_ptr<Object>()->get_shape();
            plan = descriptor.plan;
        }

        void populate(Value receiver,
                      const AttributeDeleteDescriptor &descriptor)
        {
            assert(receiver.is_ptr());
            assert(descriptor.is_cacheable());
            receiver_shape = receiver.get_ptr<Object>()->get_shape();
            plan = descriptor.plan;
        }

        void populate(Value receiver, AttributeMutationPlan mutation_plan)
        {
            assert(receiver.is_ptr());
            assert(mutation_plan.lookup_validity_cell != nullptr);
            receiver_shape = receiver.get_ptr<Object>()->get_shape();
            plan = mutation_plan;
        }

        void clear()
        {
            receiver_shape = nullptr;
            plan = AttributeWriteDescriptor::not_found().plan;
        }
    };

    static_assert(sizeof(AttributeMutationInlineCache) == 32,
                  "AttributeMutationInlineCache should stay compact");

}  // namespace cl

#endif  // CL_ATTRIBUTE_CACHE_H
