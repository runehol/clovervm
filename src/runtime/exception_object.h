#ifndef CL_EXCEPTION_OBJECT_H
#define CL_EXCEPTION_OBJECT_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"

namespace cl
{
    class ClassObject;
    class ThreadState;
    class VirtualMachine;

    class ExceptionObject : public SlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::Exception;
        static constexpr uint32_t kMessageSlot = 0;
        static constexpr uint32_t kInlineSlotCount = 1;

        ExceptionObject(ClassObject *cls, TValue<String> message)
            : ExceptionObject(cls, native_layout, message)
        {
        }

        Member<TValue<String>> message;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ExceptionObject, SlotObject, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(ExceptionObject);

    protected:
        ExceptionObject(ClassObject *cls, NativeLayoutId layout_id,
                        TValue<String> message)
            : SlotObject(cls, layout_id), message(message)
        {
        }
    };

    static_assert(CL_OFFSETOF(ExceptionObject, message) ==
                  sizeof(SlotObject) +
                      ExceptionObject::kMessageSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<ExceptionObject>);

    class StopIterationObject : public ExceptionObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::StopIteration;
        static constexpr uint32_t kValueSlot = 1;
        static constexpr uint32_t kInlineSlotCount = 2;

        StopIterationObject(ClassObject *cls,
                            Value value = Value::not_present());
        StopIterationObject(ClassObject *cls, TValue<String> message,
                            Value value = Value::not_present());

        Member<Value> value;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(StopIterationObject,
                                             ExceptionObject, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(StopIterationObject);
    };

    static_assert(CL_OFFSETOF(StopIterationObject, value) ==
                  sizeof(SlotObject) +
                      StopIterationObject::kValueSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<StopIterationObject>);

    struct Exception
    {
    };

    template <> struct TValueTraits<Exception>
    {
        using extract_type = ExceptionObject *;

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

        static extract_type extract_unchecked(Value value)
        {
            return value.get_ptr<ExceptionObject>();
        }

        static const wchar_t *target_type_name() { return L"BaseException"; }
    };

    BuiltinClassDefinition make_base_exception_class(VirtualMachine *vm);
    BuiltinClassDefinition make_exception_class(VirtualMachine *vm,
                                                ClassObject *base);
    BuiltinClassDefinition make_exception_subclass(VirtualMachine *vm,
                                                   const wchar_t *name,
                                                   ClassObject *base);
    BuiltinClassDefinition make_stop_iteration_class(VirtualMachine *vm,
                                                     ClassObject *base);

    TValue<Exception> make_exception_object(TValue<ClassObject> type,
                                            TValue<String> message);
    TValue<Exception> make_exception_object(ThreadState *thread,
                                            TValue<ClassObject> type,
                                            TValue<String> message);
    TValue<Exception> make_exception_object(TValue<ClassObject> type,
                                            const wchar_t *message);
    TValue<Exception> make_exception_object(ThreadState *thread,
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
