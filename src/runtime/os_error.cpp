#include "runtime/os_error.h"

#include "builtin_types/dict.h"
#include "builtin_types/float.h"
#include "builtin_types/int.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object_builder.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/runtime_helpers.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cerrno>
#include <iterator>

namespace cl
{
    BuiltinClassDefinition make_os_error_class(VirtualMachine *vm,
                                               const wchar_t *name,
                                               ClassObject *base,
                                               bool register_layout)
    {
        ClassObject *cls = ClassObject::make_builtin_class<OSErrorObject>(
            vm->get_or_create_interned_string_value(name),
            OSErrorObject::kInlineSlotCount, nullptr, 0, base,
            SubclassPolicy::Allowed);
        BuiltinInstanceShapeBuilder builder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            OSErrorObject::kInlineSlotCount);
        builder
            .add_slot(interned_string(L"args"), ExceptionObject::kArgsSlot,
                      descriptor_flag(DescriptorFlag::ReadOnly) |
                          descriptor_flag(DescriptorFlag::StableSlot))
            .add_slot(L"errno", OSErrorObject::kErrnoSlot)
            .add_slot(L"strerror", OSErrorObject::kStrerrorSlot)
            .add_slot(L"filename", OSErrorObject::kFilenameSlot)
            .add_slot(L"filename2", OSErrorObject::kFilename2Slot)
            .add_slot(L"characters_written",
                      OSErrorObject::kCharactersWrittenSlot);
        builder.install(mutable_attribute_shape_flags(), 5);
        if(register_layout)
        {
            static constexpr NativeLayoutId layouts[] = {
                NativeLayoutId::OSError};
            return builtin_class_definition(cls, layouts,
                                            BuiltinsVisibility::Public);
        }
        return builtin_class_definition(cls, BuiltinsVisibility::Public);
    }

    TValue<OSErrorObject> allocate_default_os_error(ThreadState *thread,
                                                    TValue<ClassObject> cls)
    {
        assert(cls.extract()->instance_native_layout_id() ==
               NativeLayoutId::OSError);
        Owned<TValue<Tuple>> args(thread->make_object_value<Tuple>(0));
        return thread->make_internal_value<OSErrorObject>(cls.extract(),
                                                          args.value());
    }

    // Inspect class namespaces only: this is constructor planning, not Python
    // attribute execution. The constructor thunk's MRO validity cell guards the
    // resolved methods; direct __new__/__init__ calls inspect the current MRO.
    static Value mro_method(ClassObject *cls, const wchar_t *name)
    {
        TValue<String> key = interned_string(name);
        Tuple *mro = native_cast<Tuple>(cls->get_mro_value());
        for(size_t i = 0; i < mro->size(); ++i)
        {
            Value value = native_cast<ClassObject>(mro->item_unchecked(i))
                              ->get_own_property(key);
            if(!value.is_not_present())
                return value;
        }
        return Value::not_present();
    }

    static bool defer_os_error_init(ThreadState *thread, ClassObject *cls)
    {
        ClassObject *base =
            thread->class_for_native_layout(NativeLayoutId::OSError);
        return mro_method(cls, L"__new__") ==
                   base->get_own_property(interned_string(L"__new__")) &&
               mro_method(cls, L"__init__") !=
                   base->get_own_property(interned_string(L"__init__"));
    }

    Expected<TValue<Dict>> make_os_error_errno_map(ThreadState *thread)
    {
        struct Entry
        {
            int code;
            const wchar_t *name;
        };
        static constexpr Entry entries[] = {
            {EAGAIN, L"BlockingIOError"},
            {EALREADY, L"BlockingIOError"},
            {EINPROGRESS, L"BlockingIOError"},
            {EWOULDBLOCK, L"BlockingIOError"},
            {EPIPE, L"BrokenPipeError"},
#ifdef ESHUTDOWN
            {ESHUTDOWN, L"BrokenPipeError"},
#endif
            {ECHILD, L"ChildProcessError"},
            {ECONNABORTED, L"ConnectionAbortedError"},
            {ECONNREFUSED, L"ConnectionRefusedError"},
            {ECONNRESET, L"ConnectionResetError"},
            {EEXIST, L"FileExistsError"},
            {ENOENT, L"FileNotFoundError"},
            {EISDIR, L"IsADirectoryError"},
            {ENOTDIR, L"NotADirectoryError"},
            {EINTR, L"InterruptedError"},
            {EACCES, L"PermissionError"},
            {EPERM, L"PermissionError"},
            {ESRCH, L"ProcessLookupError"},
            {ETIMEDOUT, L"TimeoutError"},
        };
        Owned<TValue<Dict>> mapping(thread->make_object_value<Dict>());
        // Lazy construction runs after the ordinary hash/equality helpers are
        // bootstrapped. Only frozen builtin integers are inserted, so this
        // cannot invoke the caller's errno-subclass hooks before publication.
        for(const Entry &entry: entries)
        {
            CL_TRY(mapping.extract()->set_item(
                thread, Value::from_smi(entry.code),
                Value::from_oop(thread->class_for_builtin_name(entry.name))));
        }
        return Expected<TValue<Dict>>::ok(mapping.value());
    }

    Value prepare_os_error_new(ThreadState *thread, Value cls_value,
                               Value args_value, Value kwargs_value)
    {
        TValue<ClassObject> cls =
            CL_TRY(TValue<ClassObject>::from_value_or_raise(
                cls_value, L"TypeError", L"OSError.__new__ expects a class"));
        if(cls.extract()->instance_native_layout_id() !=
           NativeLayoutId::OSError)
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"OSError.__new__ expects an OSError subtype");
        if(defer_os_error_init(thread, cls.extract()))
            return cls_value;
        if(!native_cast<Dict>(kwargs_value)->empty())
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"OSError() takes no keyword arguments");
        Tuple *args = native_cast<Tuple>(args_value);
        if(cls.extract() ==
               thread->class_for_native_layout(NativeLayoutId::OSError) &&
           args->size() >= 2 && args->size() <= 5)
        {
            Value number = args->item_unchecked(0);
            if(is_intlike_value(number))
            {
                TValue<Dict> mapping =
                    CL_TRY(thread->get_machine()->os_error_errno_map());
                // Use the same semantic lookup as Python dictionaries. In
                // particular, even an unknown or huge int-subclass errno must
                // run __hash__, and matching keys may run __eq__ and __bool__.
                Dict::ItemResult result = CL_TRY(
                    mapping.extract()->get_item_if_present(thread, number));
                if(result.found)
                    return result.value;
            }
        }
        return cls_value;
    }

    static Value initialize_os_error(ThreadState *thread,
                                     TValue<OSErrorObject> self,
                                     TValue<Tuple> args)
    {
        Owned<TValue<OSErrorObject>> retained_self(self);
        Owned<TValue<Tuple>> retained_args(args);
        size_t n = args.extract()->size();
        if(n >= 2 && n <= 5)
        {
            Value path =
                n >= 3 ? args.extract()->item_unchecked(2) : Value::None();
            ClassObject *cls = self.extract()->get_shape()->get_class();
            if(!path.is_none() &&
               cls == thread->class_for_builtin_name(L"BlockingIOError") &&
               (is_intlike_value(path) || has_native_layout<Float>(path)))
            {
                if(!is_intlike_value(path))
                    return thread->set_pending_builtin_exception_string(
                        L"TypeError", L"an integer is required");
                SmiViewStorage storage;
                Expected<int64_t> converted =
                    bigint_to_int64(intlike_value_bigint_view(path, &storage));
                if(converted.has_exception())
                {
                    // bigint_to_int64 can fail only with OverflowError. This
                    // constructor uses CPython's index-sized ValueError policy.
                    thread->clear_pending_exception();
                    return thread->set_pending_builtin_exception_string(
                        L"ValueError",
                        L"cannot fit 'int' into an index-sized integer");
                }
                int64_t count = converted.value();
                // Preserve CPython's constructor sentinel, while subsequent
                // assignments use the agreed ordinary-attribute semantics.
                if(count != -1)
                {
                    Value normalized =
                        CL_TRY(int_value_from_int64(thread, count));
                    CL_TRY(self.extract()->set_own_property(
                        interned_string(L"characters_written"), normalized));
                }
            }
            else if(!path.is_none())
            {
                CL_TRY(self.extract()->set_own_property(
                    interned_string(L"filename"), path));
                if(n == 5 && !args.extract()->item_unchecked(4).is_none())
                    CL_TRY(self.extract()->set_own_property(
                        interned_string(L"filename2"),
                        args.extract()->item_unchecked(4)));
                retained_args = thread->make_object_value<Tuple>(2);
                retained_args.extract()->initialize_item_unchecked(
                    0, args.extract()->item_unchecked(0));
                retained_args.extract()->initialize_item_unchecked(
                    1, args.extract()->item_unchecked(1));
            }
            CL_TRY(self.extract()->set_own_property(
                interned_string(L"errno"), args.extract()->item_unchecked(0)));
            CL_TRY(self.extract()->set_own_property(
                interned_string(L"strerror"),
                args.extract()->item_unchecked(1)));
        }
        else
        {
            // Reinitialization clears errno/message but retains filenames,
            // following the native OSError initializer's separate field rules.
            (void)self.extract()->delete_own_property(
                interned_string(L"errno"));
            (void)self.extract()->delete_own_property(
                interned_string(L"strerror"));
        }
        self.extract()->args = retained_args.value();
        return Value::None();
    }

    Value raise_os_error(ThreadState *thread, int error_number,
                         TValue<String> message, Value filename,
                         Value filename2)
    {
        Owned<TValue<String>> retained_message(message);
        Owned<Value> retained_filename(filename);
        Owned<Value> retained_filename2(filename2);
        Owned<Value> number(CL_TRY(int_value_from_int64(thread, error_number)));
        TValue<Dict> mapping =
            CL_TRY(thread->get_machine()->os_error_errno_map());
        Dict::ItemResult selected = CL_TRY(
            mapping.extract()->get_item_if_present(thread, number.value()));
        TValue<ClassObject> cls =
            selected.found
                ? TValue<ClassObject>::from_value_assumed(selected.value)
                : TValue<ClassObject>::from_oop(
                      thread->class_for_native_layout(NativeLayoutId::OSError));
        size_t count = !filename2.is_none() ? 5 : !filename.is_none() ? 3 : 2;
        Owned<TValue<Tuple>> args(thread->make_object_value<Tuple>(count));
        args.extract()->initialize_item_unchecked(0, number.value());
        args.extract()->initialize_item_unchecked(1,
                                                  retained_message.raw_value());
        if(count >= 3)
            args.extract()->initialize_item_unchecked(
                2, retained_filename.value());
        if(count == 5)
        {
            args.extract()->initialize_item_unchecked(3, Value::None());
            args.extract()->initialize_item_unchecked(
                4, retained_filename2.value());
        }
        Owned<TValue<OSErrorObject>> object(
            allocate_default_os_error(thread, cls));
        CL_PROPAGATE_EXCEPTION(
            initialize_os_error(thread, object.value(), args.value()));
        return thread->set_pending_exception_object(
            TValue<Exception>::from_value_assumed(object.raw_value()));
    }

    Value finish_os_error_new(ThreadState *thread, Value self, Value args)
    {
        TValue<OSErrorObject> object =
            TValue<OSErrorObject>::from_value_assumed(self);
        if(defer_os_error_init(thread,
                               object.extract()->get_shape()->get_class()))
            return Value::None();
        return initialize_os_error(thread, object,
                                   TValue<Tuple>::from_value_assumed(args));
    }

    static Value native_os_error_new(ThreadState *thread, Value cls, Value args,
                                     Value kwargs)
    {
        Owned<Value> selected(prepare_os_error_new(thread, cls, args, kwargs));
        CL_PROPAGATE_EXCEPTION(selected.value());
        Owned<TValue<OSErrorObject>> object(allocate_default_os_error(
            thread, TValue<ClassObject>::from_value_assumed(selected.value())));
        CL_PROPAGATE_EXCEPTION(
            finish_os_error_new(thread, object.raw_value(), args));
        return object.raw_value();
    }

    static Value native_os_error_init(ThreadState *thread, Value self,
                                      Value args, Value kwargs)
    {
        TValue<OSErrorObject> object =
            CL_TRY(TValue<OSErrorObject>::from_value_or_raise(
                self, L"TypeError",
                L"OSError.__init__ expects an OSError receiver"));
        if(!defer_os_error_init(thread,
                                object.extract()->get_shape()->get_class()))
            return Value::None();
        if(!native_cast<Dict>(kwargs)->empty())
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"OSError() takes no keyword arguments");
        return initialize_os_error(thread, object,
                                   TValue<Tuple>::from_value_assumed(args));
    }

    Value os_error_args_to_string(ThreadState *thread,
                                  TValue<OSErrorObject> self)
    {
        (void)thread;
        Owned<TValue<OSErrorObject>> retained(self);
        Owned<Value> number(self.extract()->errno_value.value());
        Owned<Value> message(self.extract()->strerror.value());
        Owned<Value> path(self.extract()->filename.value());
        Owned<Value> path2(self.extract()->filename2.value());
        // Presence belongs to the instance shape. Class-level None defaults
        // affect Python lookup, but must not make native error fields present.
        bool has_number = !number.value().is_not_present();
        bool has_message = !message.value().is_not_present();
        bool has_path = !path.value().is_not_present();
        bool has_path2 = !path2.value().is_not_present();
        if(!has_number)
            number = Value::None();
        if(message.value().is_not_present())
            message = Value::None();
        if(path.value().is_not_present())
            path = Value::None();
        if(path2.value().is_not_present())
            path2 = Value::None();
        if(!has_path && !(has_number && has_message))
            return exception_args_to_string(
                thread,
                TValue<Exception>::from_value_assumed(self.raw_value()));
        StringBuilder builder;
        builder.append_c_str(L"[Errno ");
        CL_PROPAGATE_EXCEPTION(builder.append_str(number.value()));
        builder.append_c_str(L"] ");
        CL_PROPAGATE_EXCEPTION(builder.append_str(message.value()));
        if(has_path)
        {
            builder.append_c_str(L": ");
            CL_PROPAGATE_EXCEPTION(builder.append_repr(path.value()));
            if(has_path2)
            {
                builder.append_c_str(L" -> ");
                CL_PROPAGATE_EXCEPTION(builder.append_repr(path2.value()));
            }
        }
        return builder.finish();
    }

    static Value native_os_error_str(ThreadState *thread, Value self)
    {
        return os_error_args_to_string(
            thread, CL_TRY(TValue<OSErrorObject>::from_value_or_raise(
                        self, L"TypeError",
                        L"OSError.__str__ expects an OSError receiver")));
    }

    // The native boundary keeps the receiver separate from the original call
    // tuple/dict, including direct __new__ calls with subclass-specific
    // keywords.
    static TValue<Function> make_os_error_method(VirtualMachine *vm,
                                                 const wchar_t *name,
                                                 IntrinsicFunction3 target)
    {
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(),
                               nullptr, interned_string(name));
        code.n_parameters() = 3;
        code.n_positional_parameters() = 1;
        code.function_signature().n_posonly_parameters = 1;
        code.function_signature().n_pos_or_kw_parameters = 0;
        code.parameter_flags() = FunctionParameterFlags::HasVarArgs |
                                 FunctionParameterFlags::HasKwArgs;

        NativeFunctionTarget native{};
        native.fixed3 = target;
        uint32_t idx = unwrap_bootstrap_expected(
            vm, code.add_native_function_target(native),
            "adding OSError method target");
        unwrap_bootstrap_expected(
            vm,
            code.emit_call_intrinsic(0, Bytecode::CallIntrinsic3, uint8_t(idx)),
            "emitting OSError method");
        unwrap_bootstrap_expected(vm, code.emit_return_or_raise_exception(0),
                                  "returning OSError method");
        return vm->make_immortal_object_value<Function>(
            TValue<CodeObject>::from_oop(unwrap_bootstrap_expected(
                vm, code.finalize(), "building OSError method")),
            Optional<TValue<String>>::none());
    }

    void install_os_error_methods(VirtualMachine *vm)
    {
        ClassObject *cls =
            active_thread()->class_for_native_layout(NativeLayoutId::OSError);
        unwrap_bootstrap_expected(
            vm,
            cls->set_own_property(
                interned_string(L"__new__"),
                make_os_error_method(vm, L"__new__", native_os_error_new)
                    .raw_value()),
            "installing OSError new");
        unwrap_bootstrap_expected(
            vm,
            cls->set_own_property(
                interned_string(L"__init__"),
                make_os_error_method(vm, L"__init__", native_os_error_init)
                    .raw_value()),
            "installing OSError init");
        for(const wchar_t *name:
            {L"errno", L"strerror", L"filename", L"filename2"})
        {
            unwrap_bootstrap_expected(
                vm, cls->set_own_property(interned_string(name), Value::None()),
                "installing OSError field default");
        }
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_os_error_str)};
        unwrap_bootstrap_expected(vm,
                                  install_builtin_intrinsic_methods(
                                      vm, cls, methods, std::size(methods)),
                                  "installing OSError formatting");
    }
}  // namespace cl
