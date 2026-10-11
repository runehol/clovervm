#ifndef CL_EXCEPTION_OBJECT_H
#define CL_EXCEPTION_OBJECT_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include <string>

namespace cl
{
    class Tuple;
    class ClassObject;
    class ThreadState;
    class VirtualMachine;

    class ExceptionObject : public InlineSlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::Exception;
        static constexpr uint32_t kArgsSlot = 0;
        static constexpr uint32_t kPredefinedInlineSlotCount = kArgsSlot + 1;
        static constexpr uint32_t kInlineSlotCount = 1;

        ExceptionObject(ClassObject *cls, TValue<Tuple> args)
            : ExceptionObject(cls, native_layout, args)
        {
        }

        Member<TValue<Tuple>> args;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ExceptionObject, InlineSlotObject,
                                             1);
        CL_DECLARE_STATIC_OBJECT_SIZE(ExceptionObject);

    protected:
        ExceptionObject(ClassObject *cls, NativeLayoutId layout_id,
                        TValue<Tuple> args)
            : InlineSlotObject(cls, layout_id), args(args)
        {
        }
    };

    static_assert(CL_OFFSETOF(ExceptionObject, args) ==
                  sizeof(InlineSlotObject) +
                      ExceptionObject::kArgsSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<ExceptionObject>);

    class StopIterationObject : public ExceptionObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::StopIteration;
        static constexpr uint32_t kValueSlot = 1;
        static constexpr uint32_t kPredefinedInlineSlotCount = kValueSlot + 1;
        static constexpr uint32_t kInlineSlotCount = 2;

        StopIterationObject(ClassObject *cls, TValue<Tuple> args);

        Member<Value> value;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(StopIterationObject,
                                             ExceptionObject, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(StopIterationObject);
    };

    static_assert(CL_OFFSETOF(StopIterationObject, value) ==
                  sizeof(InlineSlotObject) +
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

    TValue<ExceptionObject> allocate_default_exception(ThreadState *thread,
                                                       TValue<ClassObject> cls);

    void install_exception_class_methods(VirtualMachine *vm);

    BuiltinClassDefinition make_base_exception_class(VirtualMachine *vm);
    BuiltinClassDefinition make_exception_class(VirtualMachine *vm,
                                                ClassObject *base);
    BuiltinClassDefinition make_exception_subclass(VirtualMachine *vm,
                                                   const wchar_t *name,
                                                   ClassObject *base);
    BuiltinClassDefinition make_stop_iteration_class(VirtualMachine *vm,
                                                     ClassObject *base);

    // Internal factories: cls must have a compatible native exception shape.
    // No Python callbacks.
    TValue<Exception> make_exception_object(ThreadState *thread,
                                            TValue<ClassObject> cls,
                                            TValue<Tuple> args);
    TValue<Exception> make_exception_object_no_args(ThreadState *thread,
                                                    TValue<ClassObject> cls);
    TValue<Exception> make_exception_object_with_arg(ThreadState *thread,
                                                     TValue<ClassObject> cls,
                                                     Value arg);
    TValue<Exception> make_exception_object_with_string(ThreadState *thread,
                                                        TValue<ClassObject> cls,
                                                        const wchar_t *message);
    TValue<StopIterationObject>
    make_stop_iteration_object_no_value(ThreadState *thread,
                                        TValue<ClassObject> cls);
    TValue<StopIterationObject>
    make_stop_iteration_object_with_value(ThreadState *thread,
                                          TValue<ClassObject> cls, Value value);

    [[nodiscard]] Value exception_args_to_string(ThreadState *thread,
                                                 TValue<Exception> exception);
    // Handles string conversion failures locally and restores pending state.
    std::wstring format_pending_python_exception(ThreadState *thread);

}  // namespace cl

#endif  // CL_EXCEPTION_OBJECT_H
