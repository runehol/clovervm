#include "builtin_types/io_base.h"

#include "builtin_types/bytearray.h"
#include "builtin_types/bytes.h"
#include "builtin_types/dict.h"
#include "builtin_types/int.h"
#include "builtin_types/module_object.h"
#include "builtin_types/tuple.h"
#include "bytecode/code_object_builder.h"
#include "object_model/attr.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/runtime_helpers.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <iterator>

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
        Value native_raw_validate(ThreadState *thread, Value self)
        {
            if(!self.is_ptr() ||
               !is_io_base_layout(self.get_ptr<Object>()->native_layout_id()) ||
               !is_instance_of(
                   self.get_ptr<Object>(),
                   thread->class_for_native_layout(NativeLayoutId::RawIOBase)))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"RawIOBase method requires a raw stream");
            return Value::None();
        }

        Value native_buffered_validate(ThreadState *thread, Value self)
        {
            if(!self.is_ptr() ||
               !is_io_base_layout(self.get_ptr<Object>()->native_layout_id()) ||
               !is_instance_of(self.get_ptr<Object>(),
                               thread->class_for_native_layout(
                                   NativeLayoutId::BufferedIOBase)))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError",
                    L"BufferedIOBase method requires a buffered stream");
            return Value::None();
        }

        Value native_buffered_read_size(ThreadState *thread, Value size)
        {
            if(!is_intlike_value(size))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"read size must be an integer");
            SmiViewStorage storage;
            int64_t count = CL_TRY(
                bigint_to_int64(intlike_value_bigint_view(size, &storage)));
            // The abstract stubs use C int, unlike raw read's index-sized size.
            if(count < INT_MIN || count > INT_MAX)
                return thread->set_pending_builtin_exception_string(
                    L"OverflowError",
                    L"Python int too large to convert to C int");
            return Value::None();
        }

        Value native_buffered_readinto_size(ThreadState *thread, Value self,
                                            Value buffer)
        {
            CL_PROPAGATE_EXCEPTION(native_buffered_validate(thread, self));
            auto destination = CL_TRY(TValue<ByteArray>::from_value_or_raise(
                buffer, L"TypeError",
                L"readinto destination must be bytearray"));
            return Value::from_smi(
                static_cast<int64_t>(destination.extract()->size()));
        }

        Value native_buffered_finish_readinto(ThreadState *thread, Value buffer,
                                              Value data, Value requested)
        {
            Owned<TValue<ByteArray>> destination(
                CL_TRY(TValue<ByteArray>::from_value_or_raise(
                    buffer, L"TypeError",
                    L"readinto destination must be bytearray")));
            Owned<TValue<Bytes>> source(
                CL_TRY(TValue<Bytes>::from_value_or_raise(
                    data, L"TypeError", L"read() should return bytes")));
            auto bound = CL_TRY(TValue<SMI>::from_value_or_raise(
                requested, L"TypeError", L"invalid requested read size"));
            int64_t count = source.extract()->count.value().extract();
            if(count > bound.extract() ||
               static_cast<size_t>(count) > destination.extract()->size())
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"read() returned more bytes than the "
                                   L"destination can hold");
            if(count != 0)
                with_bytearray_writable_bytes(
                    destination.value(),
                    [&](std::span<uint8_t> bytes) noexcept {
                        std::copy_n(source.extract()->data,
                                    static_cast<size_t>(count), bytes.data());
                    });
            return Value::from_smi(count);
        }

        Value native_buffered_write(ThreadState *thread, Value self, Value)
        {
            CL_PROPAGATE_EXCEPTION(native_buffered_validate(thread, self));
            return thread->set_pending_builtin_exception_string(
                L"UnsupportedOperation", L"write");
        }
        Value native_buffered_detach(ThreadState *thread, Value self)
        {
            CL_PROPAGATE_EXCEPTION(native_buffered_validate(thread, self));
            return thread->set_pending_builtin_exception_string(
                L"UnsupportedOperation", L"detach");
        }

        Value native_raw_read_size(ThreadState *thread, Value size)
        {
            if(!is_intlike_value(size))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"read size must be an integer");
            SmiViewStorage storage;
            int64_t count = CL_TRY(
                bigint_to_int64(intlike_value_bigint_view(size, &storage)));
            // Canonicalize stored payloads so int-subclass comparison hooks
            // cannot affect the managed negative-size dispatch.
            return CL_TRY(int_value_from_int64(thread, count));
        }

        Value native_raw_finish_read(ThreadState *thread, Value buffer,
                                     Value count, Value requested)
        {
            Owned<TValue<ByteArray>> destination(
                CL_TRY(TValue<ByteArray>::from_value_or_raise(
                    buffer, L"TypeError",
                    L"read destination must be bytearray")));
            auto bound = CL_TRY(TValue<SMI>::from_value_or_raise(
                requested, L"TypeError", L"invalid requested read size"));
            if(!is_intlike_value(count))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"readinto result must be an integer");
            SmiViewStorage count_storage, zero_storage, bound_storage;
            auto view = intlike_value_bigint_view(count, &count_storage);
            if(bound.extract() < 0 ||
               compare_bigint(view, smi_bigint_view(0, &zero_storage)) < 0 ||
               compare_bigint(
                   view, smi_bigint_view(bound.extract(), &bound_storage)) > 0)
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"readinto returned a count outside the "
                                   L"requested buffer size");
            int64_t filled = CL_TRY(bigint_to_int64(view));
            if(static_cast<size_t>(filled) > destination.extract()->size())
                return thread->set_pending_builtin_exception_string(
                    L"ValueError",
                    L"readinto returned a count beyond the resized buffer");
            // Allocate before borrowing either payload. No Python callbacks or
            // allocations occur while copying from the current bytearray view.
            Owned<TValue<Bytes>> result(thread->make_object_value<Bytes>(
                TValue<SMI>::from_smi(filled)));
            if(filled != 0)
                with_bytearray_writable_bytes(
                    destination.value(),
                    [&](std::span<uint8_t> bytes) noexcept {
                        std::copy_n(bytes.data(), static_cast<size_t>(filled),
                                    result.extract()->data);
                    });
            return result.raw_value();
        }

        Value native_raw_errno_is_eintr(ThreadState *, Value code)
        {
            if(!is_intlike_value(code))
                return Value::False();
            SmiViewStorage storage, eintr_storage;
            return compare_bigint(intlike_value_bigint_view(code, &storage),
                                  smi_bigint_view(EINTR, &eintr_storage)) == 0
                       ? Value::True()
                       : Value::False();
        }

        Value native_raw_unimplemented(ThreadState *thread, Value self, Value)
        {
            CL_PROPAGATE_EXCEPTION(native_raw_validate(thread, self));
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
            case NativeLayoutId::BufferedIOBase:
                return thread
                    ->make_internal_value<BufferedIOBaseObject>(cls.extract())
                    .raw_value();
            default:
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"IOBase.__new__ requires an IOBase layout");
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

    static BuiltinClassDefinition make_base(VirtualMachine *vm,
                                            NativeLayoutId layout)
    {
        ClassObject *cls = nullptr;
        switch(layout)
        {
            case NativeLayoutId::IOBase:
                cls = ClassObject::make_builtin_class<IOBaseObject>(
                    vm->get_or_create_interned_string_value(L"IOBase"), 1,
                    nullptr, 0, vm->object_class(), SubclassPolicy::Allowed);
                break;
            case NativeLayoutId::RawIOBase:
                cls = ClassObject::make_builtin_class<RawIOBaseObject>(
                    vm->get_or_create_interned_string_value(L"RawIOBase"), 1,
                    nullptr, 0,
                    vm->class_for_native_layout(NativeLayoutId::IOBase),
                    SubclassPolicy::Allowed);
                break;
            case NativeLayoutId::BufferedIOBase:
                cls = ClassObject::make_builtin_class<BufferedIOBaseObject>(
                    vm->get_or_create_interned_string_value(L"BufferedIOBase"),
                    1, nullptr, 0,
                    vm->class_for_native_layout(NativeLayoutId::IOBase),
                    SubclassPolicy::Allowed);
                break;
            default:
                fatal("invalid I/O base layout");
        }
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
        static constexpr NativeLayoutId raw[] = {NativeLayoutId::RawIOBase};
        static constexpr NativeLayoutId buffered[] = {
            NativeLayoutId::BufferedIOBase};
        return layout == NativeLayoutId::IOBase
                   ? builtin_class_definition(cls, io,
                                              BuiltinsVisibility::Internal)
               : layout == NativeLayoutId::RawIOBase
                   ? builtin_class_definition(cls, raw,
                                              BuiltinsVisibility::Internal)
                   : builtin_class_definition(cls, buffered,
                                              BuiltinsVisibility::Internal);
    }
    BuiltinClassDefinition make_io_base_class(VirtualMachine *vm)
    {
        return make_base(vm, NativeLayoutId::IOBase);
    }
    BuiltinClassDefinition make_raw_io_base_class(VirtualMachine *vm)
    {
        return make_base(vm, NativeLayoutId::RawIOBase);
    }
    BuiltinClassDefinition make_buffered_io_base_class(VirtualMachine *vm)
    {
        return make_base(vm, NativeLayoutId::BufferedIOBase);
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
            builtin_intrinsic_method(L"readinto", native_raw_unimplemented),
            builtin_intrinsic_method(L"write", native_raw_unimplemented),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(
                vm, thread->class_for_native_layout(NativeLayoutId::RawIOBase),
                raw, std::size(raw)),
            "installing RawIOBase methods");
        BuiltinIntrinsicMethod buffered[] = {
            builtin_intrinsic_method(L"write", native_buffered_write),
            builtin_intrinsic_method(L"detach", native_buffered_detach),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(
                vm,
                thread->class_for_native_layout(NativeLayoutId::BufferedIOBase),
                buffered, std::size(buffered)),
            "installing BufferedIOBase methods");
    }
    void install_io_base_helpers(VirtualMachine *vm, ModuleObject *module)
    {
        BuiltinIntrinsicMethod helpers[] = {
            builtin_intrinsic_method(L"_clover_bufferedio_validate",
                                     native_buffered_validate),
            builtin_intrinsic_method(L"_clover_bufferedio_read_size",
                                     native_buffered_read_size),
            builtin_intrinsic_method(L"_clover_bufferedio_readinto_size",
                                     native_buffered_readinto_size),
            builtin_intrinsic_method(L"_clover_bufferedio_finish_readinto",
                                     native_buffered_finish_readinto),
            builtin_intrinsic_method(L"_clover_rawio_validate",
                                     native_raw_validate),
            builtin_intrinsic_method(L"_clover_rawio_read_size",
                                     native_raw_read_size),
            builtin_intrinsic_method(L"_clover_rawio_finish_read",
                                     native_raw_finish_read),
            builtin_intrinsic_method(L"_clover_rawio_errno_is_eintr",
                                     native_raw_errno_is_eintr),
        };
        for(const auto &helper: helpers)
        {
            auto function = unwrap_bootstrap_expected(
                vm, make_intrinsic_function(vm, helper),
                "creating raw I/O helper");
            unwrap_bootstrap_expected(
                vm,
                module->set_own_property(
                    vm->get_or_create_interned_string_value(helper.name),
                    function.raw_value()),
                "installing raw I/O helper");
        }
        // Keep the actual native read, independent of public module bindings or
        // Python subclass overrides. This callable receives self explicitly.
        Value read = vm->class_for_native_layout(NativeLayoutId::FileIO)
                         ->get_own_property(interned_string(L"read"));
        assert(has_native_layout<Function>(read));
        unwrap_bootstrap_expected(
            vm,
            module->set_own_property(
                interned_string(L"_clover_fileio_native_read"), read),
            "retaining FileIO native read");
    }

    void install_io_base_python_methods(VirtualMachine *vm,
                                        ModuleObject *module)
    {
        struct Binding
        {
            NativeLayoutId layout;
            const wchar_t *name;
            const wchar_t *helper;
        };
        const Binding bindings[] = {
            {NativeLayoutId::BufferedIOBase, L"read",
             L"_clover_bufferedio_read"},
            {NativeLayoutId::BufferedIOBase, L"read1",
             L"_clover_bufferedio_read1"},
            {NativeLayoutId::BufferedIOBase, L"readinto",
             L"_clover_bufferedio_readinto"},
            {NativeLayoutId::BufferedIOBase, L"readinto1",
             L"_clover_bufferedio_readinto1"},
            {NativeLayoutId::RawIOBase, L"read", L"_clover_rawio_read"},
            {NativeLayoutId::RawIOBase, L"readall", L"_clover_rawio_readall"},
            {NativeLayoutId::FileIO, L"readall", L"_clover_fileio_readall"},
        };
        for(const auto &binding: bindings)
        {
            Value function =
                module->get_own_property(interned_string(binding.helper));
            if(!has_native_layout<Function>(function))
                fatal("missing managed raw I/O method");
            bool stored =
                vm->class_for_native_layout(binding.layout)
                    ->define_own_property(
                        interned_string(binding.name), function,
                        descriptor_flag(DescriptorFlag::ReadOnly) |
                            descriptor_flag(DescriptorFlag::StableSlot));
            assert(stored);
            (void)stored;
        }
    }
}  // namespace cl
