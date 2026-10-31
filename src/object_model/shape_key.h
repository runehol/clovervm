#ifndef CL_SHAPE_KEY_H
#define CL_SHAPE_KEY_H

#include "memory/heap_reference_slot.h"
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
            if(value.is_ptr())
            {
                Shape *shape = value.get_ptr<Object>()->get_shape();
                assert(shape != nullptr);
                return ShapeKey(reinterpret_cast<uintptr_t>(shape));
            }
            return ShapeKey(uintptr_t(value.as.integer) & value_tag_mask);
        }

        static ALWAYSINLINE ShapeKey from_shape(Shape *shape)
        {
            assert(shape != nullptr);
            return ShapeKey(reinterpret_cast<uintptr_t>(shape));
        }

        constexpr bool is_valid() const { return bits != invalid_bits; }
        constexpr bool is_inline() const
        {
            return (bits & value_ptr_mask) == 0;
        }
        constexpr uintptr_t inline_tag() const
        {
            assert(is_inline());
            return bits;
        }

        bool operator==(ShapeKey other) const { return bits == other.bits; }
        bool operator!=(ShapeKey other) const { return bits != other.bits; }

        gc::WeakState repair_weak_reference(WeakResolver resolver)
        {
            if(!is_valid() || is_inline())
            {
                return gc::WeakState::Resolved;
            }
            WeakResolution resolution =
                resolver(reinterpret_cast<HeapObject *>(bits));
            switch(resolution.state)
            {
                case gc::WeakState::Resolved:
                    HeapReferenceSlot(&bits).rewrite_pointer(resolution.target);
                    break;
                case gc::WeakState::Pending:
                    break;
                case gc::WeakState::Dead:
                    *this = ShapeKey{};
                    break;
            }
            return resolution.state;
        }

    private:
        static constexpr uintptr_t invalid_bits = UINTPTR_MAX;

        explicit constexpr ShapeKey(uintptr_t _bits) : bits(_bits) {}

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
