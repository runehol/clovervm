#ifndef CL_VALIDITY_CELL_H
#define CL_VALIDITY_CELL_H

#include "native/native_layout_declarations.h"
#include "object_model/heap_object.h"

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

        CL_DECLARE_EMPTY_VALUE_SPAN(ValidityCell);
        CL_DECLARE_STATIC_OBJECT_SIZE(ValidityCell);

    private:
        bool valid;
    };
}  // namespace cl

#endif  // CL_VALIDITY_CELL_H
