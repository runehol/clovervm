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
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::TupleIterator;

        TupleIterator(ClassObject *cls, TValue<Tuple> _tuple)
            : Object(cls, native_layout_id, compact_layout()), tuple(_tuple),
              index(Value::from_smi(0))
        {
        }

        MemberTValue<Tuple> tuple;
        MemberTValue<SMI> index;

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(TupleIterator, Object, 2);
    };

    static_assert(std::is_trivially_destructible_v<TupleIterator>);

    BuiltinClassDefinition make_tuple_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_TUPLE_ITERATOR_H
