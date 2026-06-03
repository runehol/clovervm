#ifndef CL_LIST_ITERATOR_H
#define CL_LIST_ITERATOR_H

#include "builtin_types/list.h"
#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"

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
            : Object(cls, native_layout), list(_list),
              index(TValue<SMI>::from_smi(0))
        {
        }

        Member<TValue<List>> list;
        Member<TValue<SMI>> index;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ListIterator, Object, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(ListIterator);
    };

    static_assert(std::is_trivially_destructible_v<ListIterator>);

    BuiltinClassDefinition make_list_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_LIST_ITERATOR_H
