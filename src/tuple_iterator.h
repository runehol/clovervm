#ifndef CL_TUPLE_ITERATOR_H
#define CL_TUPLE_ITERATOR_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned_typed_value.h"
#include "tuple.h"
#include "value.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class TupleIterator : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::TupleIterator;

        TupleIterator(ClassObject *cls, TValue<Tuple> _tuple)
            : Object(cls, native_layout), tuple(_tuple),
              index(TValue<SMI>::from_smi(0)),
              length(TValue<SMI>::from_smi(
                  static_cast<int64_t>(_tuple.extract()->size())))
        {
        }

        MemberTValue<Tuple> tuple;
        MemberTValue<SMI> index;
        MemberTValue<SMI> length;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(TupleIterator, Object, 3);
        CL_DECLARE_STATIC_OBJECT_SIZE(TupleIterator);
    };

    static_assert(std::is_trivially_destructible_v<TupleIterator>);

    BuiltinClassDefinition make_tuple_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_TUPLE_ITERATOR_H
