#ifndef CL_RANGE_ITERATOR_H
#define CL_RANGE_ITERATOR_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned_typed_value.h"
#include "value.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class RangeIterator : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::RangeIterator;

        RangeIterator(ClassObject *cls, TValue<CLInt> _current,
                      TValue<CLInt> _stop, TValue<CLInt> _step)
            : Object(cls, native_layout_id, compact_layout()),
              current(_current), stop(_stop), step(_step)
        {
        }

        RangeIterator(TValue<CLInt> _current, TValue<CLInt> _stop,
                      TValue<CLInt> _step)
            : Object(native_layout_id, compact_layout()), current(_current),
              stop(_stop), step(_step)
        {
        }

        MemberTValue<CLInt> current;
        MemberTValue<CLInt> stop;
        MemberTValue<CLInt> step;

        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(RangeIterator, current, 3);
    };

    static_assert(std::is_trivially_destructible_v<RangeIterator>);

    BuiltinClassDefinition make_range_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_RANGE_ITERATOR_H
