#ifndef CL_SHAPE_KEY_H
#define CL_SHAPE_KEY_H

#include "object_model/value.h"
#include <cassert>
#include <cstdint>

namespace cl
{
    class VirtualMachine;

    class ShapeKey
    {
    public:
        ShapeKey() = default;

        static ALWAYSINLINE ShapeKey from_value(Value value)
        {
            value.assert_not_vm_sentinel();
            if(value.is_ptr())
            {
                Shape *shape = value.get_ptr<Object>()->get_shape();
                assert(shape != nullptr);
                return ShapeKey(reinterpret_cast<uintptr_t>(shape));
            }
            return ShapeKey(uintptr_t(value.as.integer) & value_tag_mask);
        }

        bool is_valid() const { return bits != invalid_bits; }

        bool operator==(ShapeKey other) const { return bits == other.bits; }
        bool operator!=(ShapeKey other) const { return bits != other.bits; }

    private:
        static constexpr uintptr_t invalid_bits = UINTPTR_MAX;

        explicit constexpr ShapeKey(uintptr_t _bits) : bits(_bits) {}

        bool is_inline() const { return (bits & value_ptr_mask) == 0; }

        uintptr_t inline_tag() const
        {
            assert(is_inline());
            return bits;
        }

        Shape *shape() const
        {
            assert(!is_inline());
            return reinterpret_cast<Shape *>(bits);
        }

        uintptr_t bits = invalid_bits;

        friend class VirtualMachine;
    };
}  // namespace cl

#endif  // CL_SHAPE_KEY_H
