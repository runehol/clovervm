#ifndef CL_EXCEPTION_OBJECT_H
#define CL_EXCEPTION_OBJECT_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned_typed_value.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class ExceptionObject : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::Exception;
        static constexpr uint32_t kClassSlot = 0;
        static constexpr uint32_t kMessageSlot = 1;
        static constexpr uint32_t kInlineSlotCount = 2;

        ExceptionObject(ClassObject *cls, TValue<String> message)
            : Object(cls, native_layout_id, compact_layout()), message(message)
        {
        }

        MemberTValue<String> message;

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(ExceptionObject, Object,
                                                     1);
    };

    static_assert(CL_OFFSETOF(ExceptionObject, message) ==
                  CL_OFFSETOF(Object, cls) +
                      ExceptionObject::kMessageSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<ExceptionObject>);

    BuiltinClassDefinition make_base_exception_class(VirtualMachine *vm);
    BuiltinClassDefinition make_exception_class(VirtualMachine *vm,
                                                ClassObject *base);
    BuiltinClassDefinition make_exception_subclass(VirtualMachine *vm,
                                                   const wchar_t *name,
                                                   ClassObject *base);

    TValue<ExceptionObject> make_exception_object(TValue<ClassObject> type,
                                                  TValue<String> message);
    TValue<ExceptionObject> make_exception_object(TValue<ClassObject> type,
                                                  const char *message);

}  // namespace cl

#endif  // CL_EXCEPTION_OBJECT_H
