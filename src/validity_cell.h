#ifndef CL_VALIDITY_CELL_H
#define CL_VALIDITY_CELL_H

#include "heap_object.h"

namespace cl
{
    class ValidityCell : public HeapObject
    {
    public:
        ValidityCell() : HeapObject(compact_layout()), valid(true) {}

        bool is_valid() const { return valid; }
        void invalidate() { valid = false; }

        CL_DECLARE_STATIC_LAYOUT_NO_VALUES(ValidityCell);

    private:
        bool valid;
    };
}  // namespace cl

#endif  // CL_VALIDITY_CELL_H
