#ifndef CL_ATTRIBUTE_CACHE_H
#define CL_ATTRIBUTE_CACHE_H

#include "attribute_descriptor.h"
#include "object.h"
#include "shape.h"
#include "validity_cell.h"
#include "value.h"
#include <cassert>

namespace cl
{
    struct AttributeReadInlineCache
    {
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

    struct AttributeMutationInlineCache
    {
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
