#include "builtin_types/io_base.h"

#include "builtin_types/dict.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object_builder.h"
#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/runtime_helpers.h"

namespace cl
{
    namespace
    {
        // This validates the native prefix without weakening exact-layout
        // casts.
        [[nodiscard]] Expected<TValue<IOBaseObject>> io_receiver(Value self)
        {
            if(!self.is_ptr() ||
               !is_io_base_layout(self.get_ptr<Object>()->native_layout_id()))
                return Expected<TValue<IOBaseObject>>::raise_exception(
                    L"TypeError", L"IOBase method requires an IOBase instance");
            return Expected<TValue<IOBaseObject>>::ok(
                TValue<IOBaseObject>::from_value_unchecked(self));
        }

        [[nodiscard]] Value truth(ThreadState *thread, Value value)
        {
            ClassObject *cls = thread->get_machine()->bool_class();
            return thread->call_clovervm_function(
                TValue<Function>::from_value_assumed(
                    cls->get_own_property(interned_string(L"__new__"))),
                Value::from_oop(cls), value);
        }

        Value native_io_new(ThreadState *thread, Value cls, Value args,
                            Value kwargs)
        {
            auto type = CL_TRY(TValue<ClassObject>::from_value_or_raise(
                cls, L"TypeError", L"IOBase.__new__ requires a class"));
            if(!TValue<Tuple>::from_value_assumed(args).extract()->empty() ||
               TValue<Dict>::from_value_assumed(kwargs).extract()->size() != 0)
            {
                // Like object.__new__, permit arguments for a subclass whose
                // initializer consumes them. Bare bases accept no arguments.
                auto init = resolve_attr_read_descriptor(
                    cls, interned_string(L"__init__"));
                if(!init.is_found())
                    return thread->set_pending_builtin_exception_string(
                        L"TypeError", L"IOBase() takes no arguments");
            }
            (void)type;
            return allocate_io_base(thread, cls);
        }

        Value native_io_flush(ThreadState *thread, Value self)
        {
            auto io = CL_TRY(io_receiver(self));
            if(io.extract()->base_closed.value().extract())
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"I/O operation on closed file.");
            return Value::None();
        }

        Value native_io_check_closed(ThreadState *thread, Value self)
        {
            Owned<Value> retained(self);
            (void)CL_TRY(io_receiver(self));
            Owned<Value> closed(load_attr(self, interned_string(L"closed")));
            CL_PROPAGATE_EXCEPTION(closed.value());
            if(closed.value().is_not_present())
                return thread->set_pending_builtin_exception_string(
                    L"AttributeError", L"closed");
            Value result = truth(thread, closed.value());
            CL_PROPAGATE_EXCEPTION(result);
            if(result == Value::True())
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"I/O operation on closed file.");
            return Value::None();
        }

        Value native_io_false(ThreadState *, Value self)
        {
            (void)CL_TRY(io_receiver(self));
            return Value::False();
        }

        Value native_io_isatty(ThreadState *thread, Value self)
        {
            CL_PROPAGATE_EXCEPTION(native_io_check_closed(thread, self));
            return Value::False();
        }

        template <const wchar_t *Name>
        Value native_io_check(ThreadState *thread, Value self)
        {
            Owned<Value> retained(self);
            (void)CL_TRY(io_receiver(self));
            Owned<Value> result(
                thread->call_clovervm_method(self, interned_string(Name)));
            CL_PROPAGATE_EXCEPTION(result.value());
            if(result.value() != Value::True())
                return thread->set_pending_builtin_exception_string(
                    L"UnsupportedOperation", Name);
            return Value::True();
        }
        constexpr wchar_t readable[] = L"readable";
        constexpr wchar_t writable[] = L"writable";
        constexpr wchar_t seekable[] = L"seekable";

        Value native_io_fileno(ThreadState *thread, Value self)
        {
            (void)CL_TRY(io_receiver(self));
            return thread->set_pending_builtin_exception_string(
                L"UnsupportedOperation", L"fileno");
        }
        Value native_io_seek(ThreadState *thread, Value self, Value, Value)
        {
            (void)CL_TRY(io_receiver(self));
            return thread->set_pending_builtin_exception_string(
                L"UnsupportedOperation", L"seek");
        }
        Value native_io_truncate(ThreadState *thread, Value self, Value)
        {
            (void)CL_TRY(io_receiver(self));
            return thread->set_pending_builtin_exception_string(
                L"UnsupportedOperation", L"truncate");
        }
        Value native_io_tell(ThreadState *thread, Value self)
        {
            Owned<Value> retained(self);
            (void)CL_TRY(io_receiver(self));
            return thread->call_clovervm_method(self, interned_string(L"seek"),
                                                Value::from_smi(0),
                                                Value::from_smi(1));
        }
        Value native_io_enter(ThreadState *thread, Value self)
        {
            Owned<Value> retained(self);
            CL_PROPAGATE_EXCEPTION(native_io_check_closed(thread, self));
            return self;
        }
        Value native_io_exit(ThreadState *thread, Value self, Value, Value,
                             Value)
        {
            Owned<Value> retained(self);
            (void)CL_TRY(io_receiver(self));
            return thread->call_clovervm_method(self,
                                                interned_string(L"close"));
        }
        Value native_raw_unimplemented(ThreadState *thread, Value self, Value)
        {
            if(!self.is_ptr() || (self.get_ptr<Object>()->native_layout_id() !=
                                      NativeLayoutId::RawIOBase &&
                                  self.get_ptr<Object>()->native_layout_id() !=
                                      NativeLayoutId::FileIO))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"RawIOBase method requires a raw stream");
            return thread->set_pending_builtin_exception_string(
                L"NotImplementedError", L"raw stream operation");
        }
    }  // namespace

    Value allocate_io_base(ThreadState *thread, Value cls_value)
    {
        auto cls = CL_TRY(TValue<ClassObject>::from_value_or_raise(
            cls_value, L"TypeError", L"IOBase allocator requires a class"));
        switch(cls.extract()->instance_native_layout_id())
        {
            case NativeLayoutId::IOBase:
                return thread->make_internal_value<IOBaseObject>(cls.extract())
                    .raw_value();
            case NativeLayoutId::RawIOBase:
                return thread
                    ->make_internal_value<RawIOBaseObject>(cls.extract())
                    .raw_value();
            default:
                return thread->set_pending_builtin_exception_string(
                    L"TypeError",
                    L"IOBase.__new__ requires an IOBase or RawIOBase class");
        }
    }

    Value close_io_base(ThreadState *thread, Value self)
    {
        Owned<TValue<IOBaseObject>> io(CL_TRY(io_receiver(self)));
        if(io.extract()->base_closed.value().extract())
            return Value::None();
        Value result =
            thread->call_clovervm_method(self, interned_string(L"flush"));
        // No fallible operation between the callback and committing the private
        // marker. Preserve any pending exception, including special exceptions.
        io.extract()->base_closed = TValue<Bool>::from_bool(true);
        return result.is_exception_marker() ? result : Value::None();
    }

    static BuiltinClassDefinition make_base(VirtualMachine *vm, bool raw)
    {
        ClassObject *cls =
            raw ? ClassObject::make_builtin_class<RawIOBaseObject>(
                      vm->get_or_create_interned_string_value(L"RawIOBase"), 1,
                      nullptr, 0,
                      active_thread()->class_for_native_layout(
                          NativeLayoutId::IOBase),
                      SubclassPolicy::Allowed)
                : ClassObject::make_builtin_class<IOBaseObject>(
                      vm->get_or_create_interned_string_value(L"IOBase"), 1,
                      nullptr, 0, vm->object_class(), SubclassPolicy::Allowed);
        auto readonly = descriptor_flag(DescriptorFlag::StableSlot) |
                        descriptor_flag(DescriptorFlag::ReadOnly);
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict, 1)
            .add_slot(vm->get_or_create_interned_string_value(L"closed"), 0,
                      readonly)
            .install(mutable_attribute_shape_flags());
        bool installed = cls->define_own_property(
            vm->get_or_create_interned_string_value(L"__module__"),
            vm->get_or_create_interned_string_value(L"_io").raw_value(),
            readonly);
        assert(installed);
        (void)installed;
        static constexpr NativeLayoutId io[] = {NativeLayoutId::IOBase};
        static constexpr NativeLayoutId raw_io[] = {NativeLayoutId::RawIOBase};
        return builtin_class_definition(cls, raw ? raw_io : io,
                                        BuiltinsVisibility::Internal);
    }
    BuiltinClassDefinition make_io_base_class(VirtualMachine *vm)
    {
        return make_base(vm, false);
    }
    BuiltinClassDefinition make_raw_io_base_class(VirtualMachine *vm)
    {
        return make_base(vm, true);
    }

    void install_io_base_methods(VirtualMachine *vm)
    {
        ThreadState *thread = active_thread();
        ClassObject *io =
            thread->class_for_native_layout(NativeLayoutId::IOBase);
        // A bootstrap-only wrapper with cls followed by packed *args/**kwargs.
        CodeObjectBuilder code(vm, nullptr, vm->global_builtins_module(),
                               nullptr, interned_string(L"__new__"));
        code.n_parameters() = 3;
        code.n_positional_parameters() = 1;
        code.function_signature().n_posonly_parameters = 1;
        code.function_signature().n_pos_or_kw_parameters = 0;
        code.parameter_flags() = FunctionParameterFlags::HasVarArgs |
                                 FunctionParameterFlags::HasKwArgs;
        NativeFunctionTarget target{};
        target.fixed3 = native_io_new;
        uint32_t idx = unwrap_bootstrap_expected(
            vm, code.add_native_function_target(target),
            "adding IOBase allocator");
        unwrap_bootstrap_expected(
            vm,
            code.emit_call_intrinsic(0, Bytecode::CallIntrinsic3, uint8_t(idx)),
            "emitting IOBase allocator");
        unwrap_bootstrap_expected(vm, code.emit_return_or_raise_exception(0),
                                  "returning IOBase allocator");
        auto function = vm->make_immortal_object_value<Function>(
            TValue<CodeObject>::from_oop(unwrap_bootstrap_expected(
                vm, code.finalize(), "building IOBase allocator")),
            Optional<TValue<String>>::none());
        unwrap_bootstrap_expected(
            vm,
            io->set_own_property(interned_string(L"__new__"),
                                 function.raw_value()),
            "installing IOBase allocator");
        Owned<TValue<Tuple>> zero(thread->make_object_value<Tuple>(1));
        zero.extract()->initialize_item_unchecked(0, Value::from_smi(0));
        Owned<TValue<Tuple>> none(thread->make_object_value<Tuple>(1));
        none.extract()->initialize_item_unchecked(0, Value::None());
        Owned<TValue<Tuple>> negative(thread->make_object_value<Tuple>(1));
        negative.extract()->initialize_item_unchecked(0, Value::from_smi(-1));
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"close", close_io_base),
            builtin_intrinsic_method(L"flush", native_io_flush),
            builtin_intrinsic_method(L"readable", native_io_false),
            builtin_intrinsic_method(L"writable", native_io_false),
            builtin_intrinsic_method(L"seekable", native_io_false),
            builtin_intrinsic_method(L"isatty", native_io_isatty),
            builtin_intrinsic_method(L"fileno", native_io_fileno),
            with_defaults(builtin_intrinsic_method(L"seek", native_io_seek),
                          zero.value()),
            with_defaults(
                builtin_intrinsic_method(L"truncate", native_io_truncate),
                none.value()),
            builtin_intrinsic_method(L"tell", native_io_tell),
            builtin_intrinsic_method(L"__enter__", native_io_enter),
            builtin_intrinsic_method(L"__exit__", native_io_exit),
            builtin_intrinsic_method(L"_checkClosed", native_io_check_closed),
            builtin_intrinsic_method(L"_checkReadable",
                                     native_io_check<readable>),
            builtin_intrinsic_method(L"_checkWritable",
                                     native_io_check<writable>),
            builtin_intrinsic_method(L"_checkSeekable",
                                     native_io_check<seekable>),
        };
        unwrap_bootstrap_expected(vm,
                                  install_builtin_intrinsic_methods(
                                      vm, io, methods, std::size(methods)),
                                  "installing IOBase methods");
        BuiltinIntrinsicMethod raw[] = {
            with_defaults(
                builtin_intrinsic_method(L"read", native_raw_unimplemented),
                negative.value()),
            builtin_intrinsic_method(L"readinto", native_raw_unimplemented),
            builtin_intrinsic_method(L"write", native_raw_unimplemented),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(
                vm, thread->class_for_native_layout(NativeLayoutId::RawIOBase),
                raw, std::size(raw)),
            "installing RawIOBase methods");
    }
}  // namespace cl
