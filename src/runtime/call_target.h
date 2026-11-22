#ifndef CL_CALL_TARGET_H
#define CL_CALL_TARGET_H

#include "object_model/object.h"
#include "object_model/shape.h"
#include "object_model/typed_value.h"
#include "object_model/validity_cell.h"
#include "object_model/value.h"
#include "util/compiler.h"
#include <cassert>
#include <cstdint>

namespace cl
{
    class Function;

    enum class ReceiverBinding : uint8_t
    {
        None,
        PrependCallable,
    };

    enum class CallTargetGuardKind : uint8_t
    {
        Empty,
        ExactCallable,
        ReceiverShape,
    };

    struct CallTargetGuard
    {
        // Weak identity of a callable object or an internal Shape.
        HeapObject *identity = nullptr;
        ValidityCell *validity_cell = nullptr;
        CallTargetGuardKind kind = CallTargetGuardKind::Empty;

        static CallTargetGuard exact(Object *callable,
                                     ValidityCell *cell = nullptr)
        {
            assert(callable != nullptr);
            return {callable, cell, CallTargetGuardKind::ExactCallable};
        }

        static CallTargetGuard receiver_shape(Shape *shape, ValidityCell *cell)
        {
            assert(shape != nullptr);
            assert(cell != nullptr);
            return {shape, cell, CallTargetGuardKind::ReceiverShape};
        }

        // A compatible arity must imply a populated entry at the call site.
        ALWAYSINLINE bool matches_exact(Value callable, uint32_t n_args,
                                        uint32_t cached_n_args,
                                        bool populated = true) const
        {
            // Both checks are safe for an empty slot and can share a branch.
            const bool compatible = populated & (cached_n_args == n_args);
            assert(!compatible || identity != nullptr);
            return compatible && matches_identity(callable) && has_valid_cell();
        }

        ALWAYSINLINE bool matches_shape(Value callable, uint32_t n_args,
                                        uint32_t cached_n_args) const
        {
            return cached_n_args == n_args && identity != nullptr &&
                   matches_shape_identity(callable) && has_valid_cell();
        }

        ALWAYSINLINE bool matches(Value callable) const
        {
            return identity != nullptr &&
                   (matches_identity(callable) ||
                    matches_shape_identity(callable)) &&
                   has_valid_cell();
        }

    private:
        ALWAYSINLINE bool matches_identity(Value callable) const
        {
            return static_cast<uintptr_t>(callable.as.integer) ==
                   reinterpret_cast<uintptr_t>(identity);
        }

        ALWAYSINLINE bool matches_shape_identity(Value callable) const
        {
            return callable.is_ptr() &&
                   callable.get_ptr()->get_shape() == identity;
        }

        ALWAYSINLINE bool has_valid_cell() const
        {
            return validity_cell == nullptr || validity_cell->is_valid();
        }
    };

    static_assert(sizeof(CallTargetGuard) == 24);

    struct CallTargetDescriptor
    {
        TValue<Function> function;
        CallTargetGuard guard;
        ReceiverBinding receiver_binding = ReceiverBinding::None;
    };

    [[nodiscard]] Expected<CallTargetDescriptor>
    resolve_call_target(Value callable);
}  // namespace cl

#endif  // CL_CALL_TARGET_H
