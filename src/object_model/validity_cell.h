#ifndef CL_VALIDITY_CELL_H
#define CL_VALIDITY_CELL_H

#include "memory/native_layout_declarations.h"
#include "object_model/heap_object.h"

#include <cstddef>

namespace cl
{
    class ValidityCell : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ValidityCell;

        ValidityCell() : HeapObject(native_layout), valid(true) {}

        bool is_valid() const { return valid; }
        void invalidate() { valid = false; }

        static size_t valid_offset();

        CL_DECLARE_EMPTY_VALUE_SPAN(ValidityCell);
        CL_DECLARE_STATIC_OBJECT_SIZE(ValidityCell);

    private:
        bool valid;
    };

    inline size_t ValidityCell::valid_offset()
    {
        return CL_OFFSETOF(ValidityCell, valid);
    }
}  // namespace cl

#endif  // CL_VALIDITY_CELL_H
