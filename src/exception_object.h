#ifndef CL_EXCEPTION_OBJECT_H
#define CL_EXCEPTION_OBJECT_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned.h"
#include "typed_value.h"
#include "value_state.h"

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

        ExceptionObject(ClassObject *cls, TValue2<String> message)
            : ExceptionObject(cls, native_layout, message)
        {
        }

        Member<TValue2<String>> message;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ExceptionObject, SlotObject, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(ExceptionObject);

    protected:
        ExceptionObject(ClassObject *cls, NativeLayoutId layout_id,
                        TValue2<String> message)
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
        StopIterationObject(ClassObject *cls, TValue2<String> message,
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

    template <> struct ValueTypeTraits<Exception>
    {
        using get_type = ExceptionObject *;
        static constexpr RefcountPolicy refcount_policy =
            RefcountPolicy::Always;

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

        static get_type get_unchecked(Value value)
        {
            return value.get_ptr<ExceptionObject>();
        }
    };

    template <> struct TValue2Traits<Exception>
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
    };

    BuiltinClassDefinition make_base_exception_class(VirtualMachine *vm);
    BuiltinClassDefinition make_exception_class(VirtualMachine *vm,
                                                ClassObject *base);
    BuiltinClassDefinition make_exception_subclass(VirtualMachine *vm,
                                                   const wchar_t *name,
                                                   ClassObject *base);
    BuiltinClassDefinition make_stop_iteration_class(VirtualMachine *vm,
                                                     ClassObject *base);

    TValue2<Exception> make_exception_object(TValue2<ClassObject> type,
                                             TValue2<String> message);
    TValue2<Exception> make_exception_object(ThreadState *thread,
                                             TValue2<ClassObject> type,
                                             TValue2<String> message);
    TValue2<Exception> make_exception_object(TValue2<ClassObject> type,
                                             const wchar_t *message);
    TValue2<Exception> make_exception_object(ThreadState *thread,
                                             TValue2<ClassObject> type,
                                             const wchar_t *message);
    TValue2<StopIterationObject>
    make_stop_iteration_object(TValue2<ClassObject> type,
                               Value value = Value::not_present());
    TValue2<StopIterationObject>
    make_stop_iteration_object(ThreadState *thread, TValue2<ClassObject> type,
                               Value value = Value::not_present());

}  // namespace cl

#endif  // CL_EXCEPTION_OBJECT_H
