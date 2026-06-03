#ifndef CL_RANGE_ITERATOR_H
#define CL_RANGE_ITERATOR_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class RangeIterator : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::RangeIterator;

        RangeIterator(ClassObject *cls, TValue<SMI> _current, TValue<SMI> _stop,
                      TValue<SMI> _step)
            : Object(cls, native_layout), current(_current), stop(_stop),
              step(_step)
        {
        }

        Member<TValue<SMI>> current;
        Member<TValue<SMI>> stop;
        Member<TValue<SMI>> step;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(RangeIterator, Object, 3);
        CL_DECLARE_STATIC_OBJECT_SIZE(RangeIterator);
    };

    static_assert(std::is_trivially_destructible_v<RangeIterator>);

    BuiltinClassDefinition make_range_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_RANGE_ITERATOR_H
