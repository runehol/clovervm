#ifndef CL_EXCEPTION_OBJECT_H
#define CL_EXCEPTION_OBJECT_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned_typed_value.h"

namespace cl
{
    class ClassObject;
    class ThreadState;
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
            : ExceptionObject(cls, native_layout_id, compact_layout(), message)
        {
        }

        MemberTValue<String> message;

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(ExceptionObject, Object,
                                                     1);

    protected:
        ExceptionObject(ClassObject *cls, NativeLayoutId layout_id,
                        HeapLayout layout, TValue<String> message)
            : Object(cls, layout_id, layout), message(message)
        {
        }
    };

    static_assert(CL_OFFSETOF(ExceptionObject, message) ==
                  CL_OFFSETOF(Object, cls) +
                      ExceptionObject::kMessageSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<ExceptionObject>);

    class StopIterationObject : public ExceptionObject
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::StopIteration;
        static constexpr uint32_t kValueSlot = 2;
        static constexpr uint32_t kInlineSlotCount = 3;

        StopIterationObject(ClassObject *cls,
                            Value value = Value::not_present());
        StopIterationObject(ClassObject *cls, TValue<String> message,
                            Value value = Value::not_present());

        MemberValue value;

        CL_DECLARE_STATIC_LAYOUT_EXTENDS_WITH_VALUES(StopIterationObject,
                                                     ExceptionObject, 1);
    };

    static_assert(CL_OFFSETOF(StopIterationObject, value) ==
                  CL_OFFSETOF(Object, cls) +
                      StopIterationObject::kValueSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<StopIterationObject>);

    template <>
    struct ValueTypeTraits<ExceptionObject>
        : PointerBackedValueTypeTraits<
              ExceptionObject, ExactNativeLayoutProvider<ExceptionObject>,
              RefcountPolicy::Always>
    {
        static bool is_instance(Value value)
        {
            if(!value.is_ptr())
            {
                return false;
            }
            NativeLayoutId layout_id =
                value.get_ptr<Object>()->native_layout_id();
            return layout_id == NativeLayoutId::Exception ||
                   layout_id == NativeLayoutId::StopIteration;
        }
    };

    BuiltinClassDefinition make_base_exception_class(VirtualMachine *vm);
    BuiltinClassDefinition make_exception_class(VirtualMachine *vm,
                                                ClassObject *base);
    BuiltinClassDefinition make_exception_subclass(VirtualMachine *vm,
                                                   const wchar_t *name,
                                                   ClassObject *base);
    BuiltinClassDefinition make_stop_iteration_class(VirtualMachine *vm,
                                                     ClassObject *base);

    TValue<ExceptionObject> make_exception_object(TValue<ClassObject> type,
                                                  TValue<String> message);
    TValue<ExceptionObject> make_exception_object(ThreadState *thread,
                                                  TValue<ClassObject> type,
                                                  TValue<String> message);
    TValue<ExceptionObject> make_exception_object(TValue<ClassObject> type,
                                                  const wchar_t *message);
    TValue<ExceptionObject> make_exception_object(ThreadState *thread,
                                                  TValue<ClassObject> type,
                                                  const wchar_t *message);
    TValue<StopIterationObject>
    make_stop_iteration_object(TValue<ClassObject> type,
                               Value value = Value::not_present());
    TValue<StopIterationObject>
    make_stop_iteration_object(ThreadState *thread, TValue<ClassObject> type,
                               Value value = Value::not_present());

}  // namespace cl

#endif  // CL_EXCEPTION_OBJECT_H
