#ifndef CL_VALIDITY_CELL_H
#define CL_VALIDITY_CELL_H

#include "memory/native_layout_declarations.h"
#include "object_model/heap_object.h"

#include <cstddef>
#include <cstdint>

namespace cl
{
    enum class ValidityCellDependencyMutability : uint8_t
    {
        Mutable,
        Immutable,
    };

    class ValidityCell : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ValidityCell;

        explicit ValidityCell(ValidityCellDependencyMutability mutability)
            : HeapObject(native_layout), valid(true),
              dependency_mutability_(mutability)
        {
        }

        bool is_valid() const { return valid; }
        void invalidate() { valid = false; }
        ValidityCellDependencyMutability dependency_mutability() const
        {
            return dependency_mutability_;
        }

        static size_t valid_offset();

        CL_DECLARE_EMPTY_VALUE_SPAN(ValidityCell);
        CL_DECLARE_STATIC_OBJECT_SIZE(ValidityCell);

    private:
        bool valid;
        ValidityCellDependencyMutability dependency_mutability_;
    };

    inline size_t ValidityCell::valid_offset()
    {
        return CL_OFFSETOF(ValidityCell, valid);
    }
}  // namespace cl

#endif  // CL_VALIDITY_CELL_H
