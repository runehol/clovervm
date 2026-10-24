#ifndef CL_STRING_ITERATOR_H
#define CL_STRING_ITERATOR_H

#include "builtin_types/str.h"
#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include <cstdint>

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class StringIterator : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;

        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::StringIterator;

        StringIterator(ClassObject *cls, TValue<String> _string)
            : Object(cls, native_layout), string(_string),
              index(TValue<SMI>::from_smi(0))
        {
        }

        Member<TValue<String>> string;
        Member<TValue<SMI>> index;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(StringIterator, Object, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(StringIterator);
    };

    static_assert(std::is_trivially_destructible_v<StringIterator>);

    BuiltinClassDefinition make_string_iterator_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_STRING_ITERATOR_H
