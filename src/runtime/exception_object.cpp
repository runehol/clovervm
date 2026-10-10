#include "runtime/exception_object.h"

#include "builtin_types/str.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/shape.h"
#include "object_model/value_string.h"
#include "runtime/exception_propagation.h"
#include "runtime/runtime_helpers.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <iterator>
#include <string>

namespace cl
{
    StopIterationObject::StopIterationObject(ClassObject *cls,
                                             TValue<Tuple> args)
        : ExceptionObject(cls, native_layout, args),
          value(args.extract()->empty() ? Value::None()
                                        : args.extract()->item_unchecked(0))
    {
    }

    Value exception_args_to_string(ThreadState *thread,
                                   TValue<Exception> exception)
    {
        ThreadState::ActivationScope activation(thread);
        Owned<TValue<Tuple>> args(exception.extract()->args.value());
        if(args.extract()->empty())
        {
            return thread->get_machine()
                ->get_or_create_interned_string_value(L"")
                .raw_value();
        }
        if(args.extract()->size() == 1)
        {
            Value arg = args.extract()->item_unchecked(0);
            // Native diagnostics must work before string methods are installed.
            if(can_convert_to<String>(arg) &&
               arg.get_ptr<String>()->get_shape()->get_class() ==
                   thread->class_for_native_layout(NativeLayoutId::String))
            {
                return arg;
            }
            return value_to_str_string(arg);
        }
        return value_to_repr_string(args.raw_value());
    }

    static Value native_exception_str(ThreadState *thread, Value self)
    {
        TValue<Exception> exception =
            CL_TRY(TValue<Exception>::from_value_or_raise(
                self, L"TypeError",
                L"BaseException.__str__ expects an exception receiver"));
        return exception_args_to_string(thread, exception);
    }

    static Value native_exception_repr(ThreadState *thread, Value self)
    {
        TValue<Exception> exception =
            CL_TRY(TValue<Exception>::from_value_or_raise(
                self, L"TypeError",
                L"BaseException.__repr__ expects an exception receiver"));
        Owned<TValue<Tuple>> args(exception.extract()->args.value());
        StringBuilder builder;
        builder.append_string(
            exception.extract()->get_shape()->get_class()->get_name());
        builder.append_char(L'(');
        for(size_t i = 0; i < args.extract()->size(); ++i)
        {
            if(i != 0)
            {
                builder.append_c_str(L", ");
            }
            CL_PROPAGATE_EXCEPTION(
                builder.append_repr(args.extract()->item_unchecked(i)));
        }
        builder.append_char(L')');
        return builder.finish();
    }

    static void install_exception_instance_root_shape(ClassObject *cls)
    {
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            ExceptionObject::kInlineSlotCount)
            .add_slot(interned_string(L"args"), ExceptionObject::kArgsSlot,
                      descriptor_flag(DescriptorFlag::ReadOnly) |
                          descriptor_flag(DescriptorFlag::StableSlot))
            .install(mutable_attribute_shape_flags());
    }

    static void install_stop_iteration_instance_root_shape(ClassObject *cls)
    {
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            StopIterationObject::kInlineSlotCount)
            .add_slot(interned_string(L"args"), ExceptionObject::kArgsSlot,
                      descriptor_flag(DescriptorFlag::ReadOnly) |
                          descriptor_flag(DescriptorFlag::StableSlot))
            .add_slot(L"value", StopIterationObject::kValueSlot)
            .install(mutable_attribute_shape_flags());
    }

    static ClassObject *make_exception_class_raw(VirtualMachine *vm,
                                                 const wchar_t *name,
                                                 ClassObject *base)
    {
        ClassObject *cls = ClassObject::make_builtin_class<ExceptionObject>(
            vm->get_or_create_interned_string_value(name),
            ExceptionObject::kInlineSlotCount, nullptr, 0, base);
        install_exception_instance_root_shape(cls);
        return cls;
    }

    BuiltinClassDefinition make_base_exception_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Exception};
        ClassObject *cls =
            make_exception_class_raw(vm, L"BaseException", vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_exception_class_methods(VirtualMachine *vm)
    {
        ClassObject *cls =
            vm->class_for_native_layout(NativeLayoutId::Exception);
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_exception_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_exception_repr,
                                     L"Return repr(self)."),
        };
        unwrap_bootstrap_expected(vm,
                                  install_builtin_intrinsic_methods(
                                      vm, cls, methods, std::size(methods)),
                                  "installing exception string methods");
    }

    BuiltinClassDefinition make_exception_class(VirtualMachine *vm,
                                                ClassObject *base)
    {
        return builtin_class_definition(
            make_exception_class_raw(vm, L"Exception", base),
            BuiltinsVisibility::Public);
    }

    BuiltinClassDefinition make_exception_subclass(VirtualMachine *vm,
                                                   const wchar_t *name,
                                                   ClassObject *base)
    {
        return builtin_class_definition(
            make_exception_class_raw(vm, name, base),
            BuiltinsVisibility::Public);
    }

    BuiltinClassDefinition make_stop_iteration_class(VirtualMachine *vm,
                                                     ClassObject *base)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::StopIteration};
        ClassObject *cls = ClassObject::make_builtin_class<StopIterationObject>(
            vm->get_or_create_interned_string_value(L"StopIteration"),
            StopIterationObject::kInlineSlotCount, nullptr, 0, base);
        install_stop_iteration_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    static TValue<StopIterationObject>
    allocate_stop_iteration_object(ThreadState *thread, TValue<ClassObject> cls,
                                   TValue<Tuple> args)
    {
        assert(is_subclass_of(
            cls.extract(),
            thread->class_for_native_layout(NativeLayoutId::StopIteration)));
        Owned<TValue<Tuple>> retained_args(args);
        return thread->make_internal_value<StopIterationObject>(
            cls.extract(), retained_args.value());
    }

    TValue<Exception> make_exception_object(ThreadState *thread,
                                            TValue<ClassObject> cls,
                                            TValue<Tuple> args)
    {
        assert(is_subclass_of(cls.extract(), thread->class_for_native_layout(
                                                 NativeLayoutId::Exception)));
        if(is_subclass_of(cls.extract(), thread->class_for_native_layout(
                                             NativeLayoutId::StopIteration)))
        {
            return TValue<Exception>::from_value_assumed(
                allocate_stop_iteration_object(thread, cls, args).raw_value());
        }
        Owned<TValue<Tuple>> retained_args(args);
        return TValue<Exception>::from_value_assumed(
            thread
                ->make_internal_value<ExceptionObject>(cls.extract(),
                                                       retained_args.value())
                .raw_value());
    }

    TValue<Exception> make_exception_object_no_args(ThreadState *thread,
                                                    TValue<ClassObject> cls)
    {
        Owned<TValue<Tuple>> args(thread->make_object_value<Tuple>(0));
        return make_exception_object(thread, cls, args.value());
    }

    TValue<Exception> make_exception_object_with_arg(ThreadState *thread,
                                                     TValue<ClassObject> cls,
                                                     Value arg)
    {
        arg.assert_not_vm_sentinel();
        Owned<Value> retained_arg(arg);
        Owned<TValue<Tuple>> args(thread->make_object_value<Tuple>(1));
        args.extract()->initialize_item_unchecked(0, retained_arg.raw_value());
        return make_exception_object(thread, cls, args.value());
    }

    TValue<Exception> make_exception_object_with_string(ThreadState *thread,
                                                        TValue<ClassObject> cls,
                                                        const wchar_t *message)
    {
        return make_exception_object_with_arg(
            thread, cls, string_value(thread, message).raw_value());
    }

    TValue<StopIterationObject>
    make_stop_iteration_object_no_value(ThreadState *thread,
                                        TValue<ClassObject> cls)
    {
        Owned<TValue<Tuple>> args(thread->make_object_value<Tuple>(0));
        return allocate_stop_iteration_object(thread, cls, args.value());
    }

    TValue<StopIterationObject>
    make_stop_iteration_object_with_value(ThreadState *thread,
                                          TValue<ClassObject> cls, Value value)
    {
        value.assert_not_vm_sentinel();
        Owned<Value> retained_value(value);
        Owned<TValue<Tuple>> args(thread->make_object_value<Tuple>(1));
        args.extract()->initialize_item_unchecked(0,
                                                  retained_value.raw_value());
        return allocate_stop_iteration_object(thread, cls, args.value());
    }

    std::wstring format_pending_python_exception(ThreadState *thread)
    {
        ThreadState::ActivationScope activation(thread);
        PendingExceptionKind kind = thread->pending_exception_kind();
        if(kind == PendingExceptionKind::None)
        {
            return L"InternalError: exception marker without pending exception";
        }
        Owned<Value> stop_value(kind == PendingExceptionKind::StopIteration
                                    ? thread->pending_stop_iteration_value()
                                    : Value::not_present());
        TValue<ClassObject> stop_class = TValue<ClassObject>::from_oop(
            thread->class_for_native_layout(NativeLayoutId::StopIteration));
        Owned<TValue<Exception>> exception(
            kind == PendingExceptionKind::Object
                ? thread->pending_exception_object()
            : stop_value.raw_value().is_not_present()
                ? TValue<Exception>::from_value_assumed(
                      make_stop_iteration_object_no_value(thread, stop_class)
                          .raw_value())
                : TValue<Exception>::from_value_assumed(
                      make_stop_iteration_object_with_value(
                          thread, stop_class, stop_value.raw_value())
                          .raw_value()));
        String *name =
            exception.extract()->get_shape()->get_class()->get_name().extract();
        std::wstring result(name->data, size_t(name->count.extract()));
        thread->clear_pending_exception();
        Value message = exception_args_to_string(thread, exception.value());
        if(message.is_exception_marker())
        {
            result += L": <exception str() failed>";
        }
        else
        {
            String *text = message.get_ptr<String>();
            if(text->count.extract() != 0)
            {
                result += L": ";
                result.append(text->data, size_t(text->count.extract()));
            }
        }
        thread->clear_pending_exception();
        if(kind == PendingExceptionKind::Object)
        {
            (void)thread->set_pending_exception_object(exception.value());
        }
        else if(stop_value.raw_value().is_not_present())
        {
            (void)thread->set_pending_stop_iteration_no_value();
        }
        else
        {
            (void)thread->set_pending_stop_iteration_value(
                stop_value.raw_value());
        }
        return result;
    }

}  // namespace cl
