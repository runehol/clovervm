#ifndef CL_LIST_ITERATOR_H
#define CL_LIST_ITERATOR_H

#include "builtin_class_registry.h"
#include "list.h"
#include "object.h"
#include "owned2.h"
#include "value.h"
#include "value_state.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class ListIterator : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ListIterator;

        ListIterator(ClassObject *cls, TValue2<List> _list)
            : Object(cls, native_layout), list(_list),
              index(TValue2<SMI>::from_smi(0))
        {
        }

        Member2<TValue2<List>> list;
        Member2<TValue2<SMI>> index;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ListIterator, Object, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(ListIterator);
    };

    static_assert(std::is_trivially_destructible_v<ListIterator>);

    BuiltinClassDefinition make_list_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_LIST_ITERATOR_H
