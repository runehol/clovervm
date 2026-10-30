#include "builtin_types/file_io.h"

#include "builtin_types/bytearray.h"
#include "builtin_types/bytes.h"
#include "builtin_types/int.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "builtin_types/unicode.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "platform/file_io.h"
#include "runtime/exception_propagation.h"
#include "runtime/os_error.h"
#include "runtime/runtime_helpers.h"
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>

namespace cl
{
    namespace
    {
        // Only a descriptor opened by this construction attempt enters this
        // guard. Existing caller descriptors are never closed on rollback.
        class PendingFileDescriptor
        {
        public:
            int fd = -1;
            ~PendingFileDescriptor()
            {
                if(fd >= 0)
                {
                    int saved = errno;
                    (void)platform_file_close(fd);
                    errno = saved;
                }
            }
            int release()
            {
                int result = fd;
                fd = -1;
                return result;
            }
        };

        [[nodiscard]] Value file_os_error(ThreadState *thread, int error,
                                          Value filename = Value::None())
        {
            auto message =
                try_make_string_from_utf8(thread, std::strerror(error));
            if(!message.has_value())
                return thread->set_pending_builtin_exception_string(
                    L"ValueError",
                    L"native extension error message must be valid UTF-8");
            return raise_os_error(thread, error, *message, filename,
                                  Value::None());
        }

        [[nodiscard]] Expected<int64_t> file_integer(Value value)
        {
            if(!is_intlike_value(value))
                return Expected<int64_t>::raise_exception(
                    L"TypeError", L"an integer is required");
            SmiViewStorage storage;
            return bigint_to_int64(intlike_value_bigint_view(value, &storage));
        }

        [[nodiscard]] Expected<TValue<FileIOObject>> file_receiver(Value self)
        {
            return TValue<FileIOObject>::from_value_or_raise(
                self, L"TypeError", L"FileIO method expects a FileIO receiver");
        }

        [[nodiscard]] Expected<TValue<FileIOObject>>
        open_file_receiver(Value self)
        {
            TValue<FileIOObject> file = CL_TRY(file_receiver(self));
            if(file.extract()->fd < 0)
                return Expected<TValue<FileIOObject>>::raise_exception(
                    L"ValueError", L"I/O operation on closed file");
            return Expected<TValue<FileIOObject>>::ok(file);
        }

        struct ParsedMode
        {
            uint8_t flags;
            std::wstring canonical;
        };

        [[nodiscard]] Expected<ParsedMode> parse_mode(Value value)
        {
            TValue<String> text = CL_TRY(TValue<String>::from_value_or_raise(
                value, L"TypeError", L"mode must be a string"));
            std::wstring_view mode = string_view(text);
            wchar_t primary = 0;
            bool plus = false;
            for(wchar_t ch: mode)
            {
                if(ch == L'r' || ch == L'w' || ch == L'x' || ch == L'a')
                {
                    if(primary)
                        return Expected<ParsedMode>::raise_exception(
                            L"ValueError", L"invalid FileIO mode");
                    primary = ch;
                }
                else if(ch == L'+' && !plus)
                    plus = true;
                else if(ch == L'b')
                    continue;
                else
                    return Expected<ParsedMode>::raise_exception(
                        L"ValueError", L"invalid FileIO mode");
            }
            if(!primary)
                return Expected<ParsedMode>::raise_exception(
                    L"ValueError", L"invalid FileIO mode");
            uint8_t flags = primary == L'r' ? FileReadable : FileWritable;
            if(plus)
                flags |= FileReadable | FileWritable;
            if(primary != L'r')
                flags |= FileCreating;
            if(primary == L'w')
                flags |= FileTruncating;
            if(primary == L'x')
                flags |= FileExclusive;
            if(primary == L'a')
                flags |= FileAppending;
            // CPython reports update read/write streams as rb+, including w+.
            std::wstring canonical(1, primary == L'w' && plus ? L'r' : primary);
            canonical += L'b';
            if(plus)
                canonical += L'+';
            return Expected<ParsedMode>::ok({flags, std::move(canonical)});
        }

        Value native_file_new(ThreadState *thread, Value cls, Value source,
                              Value mode_value, Value closefd_value)
        {
            Owned<TValue<ClassObject>> type(
                CL_TRY(TValue<ClassObject>::from_value_or_raise(
                    cls, L"TypeError", L"FileIO.__new__ requires a class")));
            if(type.extract()->instance_native_layout_id() !=
               NativeLayoutId::FileIO)
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"FileIO.__new__ expects a FileIO subclass");
            Owned<Value> retained_source(source);
            Owned<Value> retained_mode(mode_value);
            Owned<Value> retained_closefd(closefd_value);
            TValue<String> mode_text =
                CL_TRY(TValue<String>::from_value_or_raise(
                    mode_value, L"TypeError", L"mode must be a string"));
            if(string_view(mode_text).find(L'\0') != std::wstring_view::npos)
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"embedded null character");
            // Reuse the canonical bool constructor's managed truth dispatch.
            ClassObject *bool_cls = thread->get_machine()->bool_class();
            Value bool_new =
                bool_cls->get_own_property(interned_string(L"__new__"));
            Value ownership = thread->call_clovervm_function(
                TValue<Function>::from_value_assumed(bool_new),
                Value::from_oop(bool_cls), closefd_value);
            CL_PROPAGATE_EXCEPTION(ownership);
            bool owns = ownership == Value::True();
            bool from_path = !is_intlike_value(source);
            int fd = -1;
            if(from_path)
            {
                (void)CL_TRY(TValue<String>::from_value_or_raise(
                    source, L"TypeError",
                    L"FileIO expects a string path or integer descriptor"));
            }
            else
            {
                int64_t number = CL_TRY(file_integer(source));
                if(number < 0)
                    return thread->set_pending_builtin_exception_string(
                        L"ValueError", L"negative file descriptor");
                if(number > INT_MAX)
                    return thread->set_pending_builtin_exception_string(
                        L"OverflowError",
                        L"file descriptor outside C int range");
                fd = static_cast<int>(number);
            }
            ParsedMode mode = CL_TRY(parse_mode(mode_value));
            Owned<TValue<String>> normalized_mode(
                string_value(thread, mode.canonical));
            Owned<TValue<FileIOObject>> file(
                thread->make_internal_value<FileIOObject>(type.extract()));
            PendingFileDescriptor pending;
            if(from_path)
            {
                TValue<String> path =
                    CL_TRY(TValue<String>::from_value_or_raise(
                        source, L"TypeError",
                        L"FileIO expects a string path or integer descriptor"));
                if(!owns)
                    return thread->set_pending_builtin_exception_string(
                        L"ValueError",
                        L"Cannot use closefd=False with file name");
                auto text = string_view(path);
                if(text.find(L'\0') != std::wstring_view::npos)
                    return thread->set_pending_builtin_exception_string(
                        L"ValueError", L"embedded null character");
                std::string encoded = unicode::encode_utf8(text);
                FileResult opened =
                    platform_file_open(encoded.c_str(), mode.flags);
                if(opened.error)
                    return file_os_error(thread, opened.error, source);
                pending.fd = fd = static_cast<int>(opened.value);
            }
            FileResult valid = platform_file_validate(fd);
            if(valid.error)
                return file_os_error(thread, valid.error,
                                     valid.error == EISDIR ? source
                                                           : Value::None());
            if(mode.flags & FileAppending)
            {
                FileResult seek = platform_file_seek(fd, 0, 2);
                if(seek.error && seek.error != ESPIPE)
                    return file_os_error(thread, seek.error);
            }
            // Commit only after every fallible step. No allocation/callbacks
            // remain after ownership transfers from caller or local guard.
            file.extract()->name = source;
            file.extract()->mode = normalized_mode.raw_value();
            file.extract()->closefd = TValue<Bool>::from_bool(owns);
            file.extract()->access_flags = mode.flags;
            file.extract()->fd = from_path ? pending.release() : fd;
            file.extract()->closed = TValue<Bool>::from_bool(false);
            return file.raw_value();
        }

        Value native_file_close(ThreadState *thread, Value self)
        {
            Owned<TValue<FileIOObject>> file(CL_TRY(file_receiver(self)));
            Value flushed = close_io_base(thread, self);
            int fd = file.extract()->detach_fd();
            if(fd >= 0 && file.extract()->closefd.value().extract())
            {
                FileResult result = platform_file_close(fd);
                if(result.error)
                {
                    if(flushed.is_exception_marker())
                        thread->clear_pending_exception();
                    return file_os_error(thread, result.error);
                }
            }
            return flushed;
        }

        Value native_file_fileno(ThreadState *thread, Value self)
        {
            auto file = CL_TRY(open_file_receiver(self));
            return CL_TRY(int_value_from_int64(thread, file.extract()->fd));
        }

        Value native_file_read(ThreadState *thread, Value self,
                               Value size_value)
        {
            Owned<TValue<FileIOObject>> file(CL_TRY(open_file_receiver(self)));
            if(!(file.extract()->access_flags & FileReadable))
                return thread->set_pending_builtin_exception_string(
                    L"UnsupportedOperation", L"File not open for reading");
            int64_t size =
                size_value.is_none() ? -1 : CL_TRY(file_integer(size_value));
            if(size < 0)
                return thread->set_pending_builtin_exception_string(
                    L"NotImplementedError",
                    L"unbounded FileIO.read is not implemented");
            if(static_cast<uint64_t>(size) >
               std::numeric_limits<size_t>::max() - sizeof(Bytes))
                return thread->set_pending_builtin_exception_string(
                    L"OverflowError", L"read size is too large");
            std::unique_ptr<uint8_t, decltype(&std::free)> buffer(
                static_cast<uint8_t *>(
                    std::malloc(size ? static_cast<size_t>(size) : 1)),
                &std::free);
            if(!buffer)
                return thread->set_pending_builtin_exception_none(
                    L"MemoryError");
            FileResult read = platform_file_read(
                file.extract()->fd, buffer.get(), static_cast<size_t>(size));
            if(read.error == EAGAIN || read.error == EWOULDBLOCK)
                return Value::None();
            if(read.error)
                return file_os_error(thread, read.error);
            return thread
                ->make_object_value<Bytes>(std::span<const uint8_t>(
                    buffer.get(), static_cast<size_t>(read.value)))
                .raw_value();
        }

        Value native_file_readinto(ThreadState *thread, Value self, Value data)
        {
            Owned<TValue<FileIOObject>> file(CL_TRY(file_receiver(self)));
            // Like CPython's writable-buffer argument binding, validate the
            // destination before checking the descriptor's state or mode.
            Owned<TValue<ByteArray>> destination(
                CL_TRY(TValue<ByteArray>::from_value_or_raise(
                    data, L"TypeError",
                    L"FileIO.readinto expects a bytearray")));
            if(file.extract()->fd < 0)
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"I/O operation on closed file");
            if(!(file.extract()->access_flags & FileReadable))
                return thread->set_pending_builtin_exception_string(
                    L"UnsupportedOperation", L"File not open for reading");
            FileResult result{};
            with_bytearray_writable_bytes(
                destination.value(), [&](std::span<uint8_t> bytes) noexcept {
                    // No VM allocation, callbacks, safepoints, or resizing
                    // while this span is borrowed, including for empty buffers.
                    result = platform_file_read(file.extract()->fd,
                                                bytes.data(), bytes.size());
                });
            if(result.error == EAGAIN || result.error == EWOULDBLOCK)
                return Value::None();
            if(result.error)
                return file_os_error(thread, result.error);
            return CL_TRY(int_value_from_int64(thread, result.value));
        }

        Value native_file_write(ThreadState *thread, Value self, Value data)
        {
            Owned<TValue<FileIOObject>> file(CL_TRY(open_file_receiver(self)));
            if(!(file.extract()->access_flags & FileWritable))
                return thread->set_pending_builtin_exception_string(
                    L"UnsupportedOperation", L"File not open for writing");
            Owned<TValue<Bytes>> bytes(
                CL_TRY(TValue<Bytes>::from_value_or_raise(
                    data, L"TypeError", L"FileIO.write expects bytes")));
            auto view = bytes_view(bytes.value());
            FileResult write = platform_file_write(file.extract()->fd,
                                                   view.data(), view.size());
            if(write.error == EAGAIN || write.error == EWOULDBLOCK)
                return Value::None();
            if(write.error)
                return file_os_error(thread, write.error);
            return CL_TRY(int_value_from_int64(thread, write.value));
        }

        Value native_file_seek(ThreadState *thread, Value self,
                               Value offset_value, Value whence_value)
        {
            Owned<TValue<FileIOObject>> file(CL_TRY(open_file_receiver(self)));
            int64_t offset = CL_TRY(file_integer(offset_value));
            int64_t whence = CL_TRY(file_integer(whence_value));
            if(whence < 0)
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"invalid whence");
            if(whence > INT_MAX)
                return thread->set_pending_builtin_exception_string(
                    L"OverflowError", L"whence outside C int range");
            FileResult result = platform_file_seek(file.extract()->fd, offset,
                                                   static_cast<int>(whence));
            if(result.error)
                return file_os_error(thread, result.error);
            file.extract()->seekable = 1;
            return CL_TRY(int_value_from_int64(thread, result.value));
        }

        Value native_file_tell(ThreadState *thread, Value self)
        {
            return native_file_seek(thread, self, Value::from_smi(0),
                                    Value::from_smi(1));
        }

        Value native_file_readable(ThreadState *thread, Value self)
        {
            auto file = CL_TRY(open_file_receiver(self));
            return ((file.extract()->access_flags & FileReadable)
                        ? Value::True()
                        : Value::False());
        }
        Value native_file_writable(ThreadState *thread, Value self)
        {
            auto file = CL_TRY(open_file_receiver(self));
            return ((file.extract()->access_flags & FileWritable)
                        ? Value::True()
                        : Value::False());
        }
        Value native_file_seekable(ThreadState *thread, Value self)
        {
            auto file = CL_TRY(open_file_receiver(self));
            if(file.extract()->seekable < 0)
            {
                FileResult result =
                    platform_file_seek(file.extract()->fd, 0, 1);
                file.extract()->seekable = result.error ? 0 : 1;
            }
            return file.extract()->seekable != 0 ? Value::True()
                                                 : Value::False();
        }
    }  // namespace

    BuiltinClassDefinition make_file_io_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class<FileIOObject>(
            vm->get_or_create_interned_string_value(L"FileIO"),
            FileIOObject::kInlineSlotCount, nullptr, 0,
            active_thread()->class_for_native_layout(NativeLayoutId::RawIOBase),
            SubclassPolicy::Allowed);
        auto readonly = descriptor_flag(DescriptorFlag::StableSlot) |
                        descriptor_flag(DescriptorFlag::ReadOnly);
        BuiltinInstanceShapeBuilder(
            cls, BuiltinInstanceShapeDefaults::DunderClassAndDict,
            FileIOObject::kInlineSlotCount)
            .reserve_slot(0)
            .add_slot(L"name", 1)
            .add_slot(vm->get_or_create_interned_string_value(L"mode"), 2,
                      readonly)
            .add_slot(vm->get_or_create_interned_string_value(L"closed"), 3,
                      readonly)
            .add_slot(vm->get_or_create_interned_string_value(L"closefd"), 4,
                      readonly)
            .install(mutable_attribute_shape_flags());
        bool installed = cls->define_own_property(
            vm->get_or_create_interned_string_value(L"__module__"),
            vm->get_or_create_interned_string_value(L"_io").raw_value(),
            readonly);
        assert(installed);
        (void)installed;
        static constexpr NativeLayoutId layouts[] = {NativeLayoutId::FileIO};
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Internal);
    }

    BuiltinClassDefinition make_unsupported_operation_class(VirtualMachine *vm)
    {
        ThreadState *thread = active_thread();
        ClassObject *os = thread->class_for_builtin_name(L"OSError");
        ClassObject *value = thread->class_for_builtin_name(L"ValueError");
        auto definition = make_os_error_class(vm, L"UnsupportedOperation", os);
        definition.builtins_visibility = BuiltinsVisibility::Internal;
        bool installed = definition.cls->define_own_property(
            vm->get_or_create_interned_string_value(L"__module__"),
            vm->get_or_create_interned_string_value(L"io").raw_value(),
            descriptor_flag(DescriptorFlag::ReadOnly) |
                descriptor_flag(DescriptorFlag::StableSlot));
        assert(installed);
        (void)installed;
        // Bootstrap C3 order for the known compatible OSError/ValueError pair.
        std::vector<ClassObject *> bases = {os, value};
        std::vector<ClassObject *> mro = {
            definition.cls,
            os,
            value,
            thread->class_for_builtin_name(L"Exception"),
            thread->class_for_builtin_name(L"BaseException"),
            vm->object_class()};
        Owned<Value> bases_tuple(tuple_from_vector(bases));
        Owned<Value> mro_tuple(tuple_from_vector(mro));
        definition.cls->install_bootstrap_inheritance(bases_tuple.value(),
                                                      mro_tuple.value());
        return definition;
    }

    void install_file_io_methods(VirtualMachine *vm)
    {
        ThreadState *thread = active_thread();
        Owned<TValue<Tuple>> constructor_defaults(
            thread->make_object_value<Tuple>(2));
        constructor_defaults.extract()->initialize_item_unchecked(
            0, vm->get_or_create_interned_string_value(L"r").raw_value());
        constructor_defaults.extract()->initialize_item_unchecked(
            1, Value::True());
        Owned<TValue<Tuple>> read_defaults(thread->make_object_value<Tuple>(1));
        read_defaults.extract()->initialize_item_unchecked(0,
                                                           Value::from_smi(-1));
        Owned<TValue<Tuple>> seek_defaults(thread->make_object_value<Tuple>(1));
        seek_defaults.extract()->initialize_item_unchecked(0,
                                                           Value::from_smi(0));
        static constexpr const wchar_t *names[] = {L"file", L"mode",
                                                   L"closefd"};
        BuiltinIntrinsicMethod methods[] = {
            with_keyword_parameter_names(
                with_defaults(
                    builtin_intrinsic_method(L"__new__", native_file_new),
                    constructor_defaults.value()),
                names, 3, 1),
            builtin_intrinsic_method(L"close", native_file_close),
            builtin_intrinsic_method(L"fileno", native_file_fileno),
            with_defaults(builtin_intrinsic_method(L"read", native_file_read),
                          read_defaults.value()),
            builtin_intrinsic_method(L"readinto", native_file_readinto),
            builtin_intrinsic_method(L"write", native_file_write),
            with_defaults(builtin_intrinsic_method(L"seek", native_file_seek),
                          seek_defaults.value()),
            builtin_intrinsic_method(L"tell", native_file_tell),
            builtin_intrinsic_method(L"readable", native_file_readable),
            builtin_intrinsic_method(L"writable", native_file_writable),
            builtin_intrinsic_method(L"seekable", native_file_seekable),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(
                vm, thread->class_for_native_layout(NativeLayoutId::FileIO),
                methods, std::size(methods)),
            "installing FileIO methods");
    }
}  // namespace cl
