#ifndef CL_RANGE_ITERATOR_H
#define CL_RANGE_ITERATOR_H

#include "klass.h"
#include "object.h"
#include "owned_value.h"
#include "value.h"

namespace cl
{

    class RangeIterator : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"RangeIterator", nullptr);

        RangeIterator(Value _current, Value _stop, Value _step)
            : Object(&klass, 3, sizeof(RangeIterator) / 8), current(_current),
              stop(_stop), step(_step)
        {
        }

        OwnedValue current;
        OwnedValue stop;
        OwnedValue step;
    };

}  // namespace cl

#endif  // CL_RANGE_ITERATOR_H
