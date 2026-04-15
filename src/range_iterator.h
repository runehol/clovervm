#ifndef CL_RANGE_ITERATOR_H
#define CL_RANGE_ITERATOR_H

#include "klass.h"
#include "object.h"
#include "owned_typed_value.h"
#include "value.h"

namespace cl
{

    class RangeIterator : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"RangeIterator", nullptr);

        RangeIterator(TValue<CLInt> _current, TValue<CLInt> _stop,
                      TValue<CLInt> _step)
            : Object(&klass, compact_layout()), current(_current), stop(_stop),
              step(_step)
        {
        }

        MemberTValue<CLInt> current;
        MemberTValue<CLInt> stop;
        MemberTValue<CLInt> step;

        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(RangeIterator, current, 3);
    };

    static_assert(std::is_trivially_destructible_v<RangeIterator>);

}  // namespace cl

#endif  // CL_RANGE_ITERATOR_H
