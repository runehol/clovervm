#ifndef CL_LIST_ITERATOR_H
#define CL_LIST_ITERATOR_H

#include "builtin_class_registry.h"
#include "list.h"
#include "object.h"
#include "owned_typed_value.h"
#include "value.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class ListIterator : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ListIterator;

        ListIterator(ClassObject *cls, TValue<List> _list)
            : Object(cls, native_layout, compact_layout()), list(_list),
              index(Value::from_smi(0))
        {
        }

        MemberTValue<List> list;
        MemberTValue<SMI> index;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ListIterator, Object, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(ListIterator);

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(ListIterator, Object, 2);
    };

    static_assert(std::is_trivially_destructible_v<ListIterator>);

    BuiltinClassDefinition make_list_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_LIST_ITERATOR_H
