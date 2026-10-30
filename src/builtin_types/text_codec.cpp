#include "builtin_types/text_codec.h"

#include "builtin_types/bytes.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "codecs/incremental_codec.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_object.h"
#include "runtime/exception_propagation.h"
#include "runtime/runtime_helpers.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace cl
{
    namespace
    {
        static_assert(sizeof(cl_wchar) == sizeof(char32_t));
        static_assert(std::numeric_limits<cl_wchar>::max() >= 0x10ffff);
        constexpr size_t kInputWords = 4096;
        enum class Direction
        {
            Encode,
            Decode
        };

        const wchar_t *encoding_name(ClCodecEncoding encoding)
        {
            return encoding == CL_CODEC_UTF8 ? L"utf-8" : L"latin-1";
        }

        [[nodiscard]] Value codec_failure(ThreadState *thread,
                                          ClCodecStatus status)
        {
            return thread->set_pending_builtin_exception_string(
                status == CL_CODEC_BACKEND_ERROR   ? L"RuntimeError"
                : status == CL_CODEC_SIZE_OVERFLOW ? L"OverflowError"
                                                   : L"SystemError",
                L"native text codec could not complete conversion");
        }

        [[nodiscard]] Expected<ClCodecEncoding>
        lookup_encoding(TValue<String> name)
        {
            // Match CPython's normalization: collapse punctuation, strip edge
            // separators, preserve dots, and lowercase ASCII letters.
            std::string normalized;
            bool separator = false;
            for(cl_wchar ch: string_view(name))
            {
                if((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                   (ch >= L'0' && ch <= L'9') || ch == L'.')
                {
                    if(separator && !normalized.empty())
                        normalized += '_';
                    separator = false;
                    normalized += char(
                        ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch);
                }
                else
                    separator = true;
            }
            // Canonical module names are accepted directly, but are not alias
            // keys. In particular, utf.8 and latin.1 must remain unrecognized.
            if(normalized == "utf_8")
                return Expected<ClCodecEncoding>::ok(CL_CODEC_UTF8);
            if(normalized == "latin_1")
                return Expected<ClCodecEncoding>::ok(CL_CODEC_LATIN1);
            // CPython retries alias-table lookup with dots changed to
            // underscores.
            std::replace(normalized.begin(), normalized.end(), '.', '_');
            constexpr std::string_view utf8[] = {
                "utf8", "cp65001", "u8", "utf", "utf8_ucs2", "utf8_ucs4"};
            constexpr std::string_view latin1[] = {
                "latin1",          "8859",       "cp819",     "csisolatin1",
                "ibm819",          "iso8859",    "iso8859_1", "iso_8859_1",
                "iso_8859_1_1987", "iso_ir_100", "l1",        "latin"};
            for(auto alias: utf8)
                if(normalized == alias)
                    return Expected<ClCodecEncoding>::ok(CL_CODEC_UTF8);
            for(auto alias: latin1)
                if(normalized == alias)
                    return Expected<ClCodecEncoding>::ok(CL_CODEC_LATIN1);
            std::wstring message = L"unknown encoding: ";
            message += string_view(name);
            return Expected<ClCodecEncoding>::raise_exception(L"LookupError",
                                                              message.c_str());
        }

        [[nodiscard]] Value
        raise_unicode_error(ThreadState *thread, Direction direction,
                            ClCodecEncoding encoding, Value input, size_t start,
                            size_t end, const wchar_t *reason)
        {
            Owned<Value> source(input);
            Owned<TValue<String>> name(
                string_value(thread, encoding_name(encoding)));
            Owned<TValue<String>> detail(string_value(thread, reason));
            Owned<TValue<Tuple>> args(thread->make_object_value<Tuple>(5));
            args.extract()->initialize_item_unchecked(0, name.raw_value());
            args.extract()->initialize_item_unchecked(1, source.value());
            args.extract()->initialize_item_unchecked(
                2, Value::from_smi(int64_t(start)));
            args.extract()->initialize_item_unchecked(
                3, Value::from_smi(int64_t(end)));
            args.extract()->initialize_item_unchecked(4, detail.raw_value());
            auto cls =
                TValue<ClassObject>::from_oop(thread->class_for_builtin_name(
                    direction == Direction::Encode ? L"UnicodeEncodeError"
                                                   : L"UnicodeDecodeError"));
            Owned<TValue<Exception>> error(
                make_exception_object(thread, cls, args.value()));
            constexpr const wchar_t *fields[] = {L"encoding", L"object",
                                                 L"start", L"end", L"reason"};
            // These are fresh instances of exact frozen builtin classes. Own
            // storage initialization cannot encounter Python subclass
            // overrides.
            for(size_t i = 0; i < 5; ++i)
                if(!CL_TRY(error.extract()->set_own_property(
                       interned_string(fields[i]),
                       args.extract()->item_unchecked(i))))
                    return thread->set_pending_builtin_exception_string(
                        L"SystemError",
                        L"could not initialize Unicode error fields");
            return thread->set_pending_exception_object(error.value());
        }

        [[nodiscard]] Value encode_error(ThreadState *thread,
                                         TValue<String> input,
                                         ClCodecEncoding encoding, size_t start)
        {
            size_t end = start + 1;
            size_t size = input.extract()->count.extract();
            auto invalid = [encoding](cl_wchar word) {
                return encoding == CL_CODEC_LATIN1
                           ? uint32_t(word) >= 256
                           : word >= 0xd800 && word <= 0xdfff;
            };
            while(end < size && invalid(input.extract()->data[end]))
                ++end;
            return raise_unicode_error(thread, Direction::Encode, encoding,
                                       input.raw_value(), start, end,
                                       encoding == CL_CODEC_LATIN1
                                           ? L"ordinal not in range(256)"
                                           : L"surrogates not allowed");
        }

        const wchar_t *decode_reason(ClCodecError error)
        {
            switch(error)
            {
                case CL_CODEC_INVALID_START_BYTE:
                    return L"invalid start byte";
                case CL_CODEC_INVALID_CONTINUATION_BYTE:
                    return L"invalid continuation byte";
                case CL_CODEC_UNEXPECTED_END:
                    return L"unexpected end of data";
                default:
                    return L"invalid Unicode input";
            }
        }

        template <Direction Dir>
        [[nodiscard]] Value native_codec(ThreadState *thread, Value self,
                                         Value encoding, Value errors)
        {
            Owned<Value> input(self);
            if constexpr(Dir == Direction::Encode)
            {
                if(!has_native_layout<String>(self))
                    return thread->set_pending_builtin_exception_string(
                        L"TypeError", L"str.encode requires a str receiver");
            }
            else if(!has_native_layout<Bytes>(self))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"bytes.decode requires a bytes receiver");
            const Value options[] = {encoding, errors};
            for(size_t option = 0; option < 2; ++option)
            {
                auto text = CL_TRY(TValue<String>::from_value_or_raise(
                    options[option], L"TypeError",
                    option == 0 ? L"encoding must be str"
                                : L"errors must be str"));
                bool has_null = false;
                auto view = string_view(text);
                for(size_t i = 0; i < view.size(); ++i)
                {
                    if(view[i] >= 0xd800 && view[i] <= 0xdfff)
                        return encode_error(thread, text, CL_CODEC_UTF8, i);
                    has_null |= view[i] == 0;
                }
                if(has_null)
                    return thread->set_pending_builtin_exception_string(
                        L"ValueError", L"embedded null character");
            }
            auto name = TValue<String>::from_value_assumed(encoding);
            auto policy = TValue<String>::from_value_assumed(errors);
            ClCodecEncoding codec = CL_CODEC_UTF8;
            // CPython's empty bytes shortcut still validates option types/NULs,
            // but does not resolve the encoding name.
            if constexpr(Dir == Direction::Encode)
                codec = CL_TRY(lookup_encoding(name));
            else if(native_cast<Bytes>(input.value())->count.extract() != 0)
                codec = CL_TRY(lookup_encoding(name));
            if(string_view(policy) != L"strict")
                return thread->set_pending_builtin_exception_string(
                    L"NotImplementedError",
                    L"only strict codec errors are supported");
            if constexpr(Dir == Direction::Encode)
                return encode_string(
                    thread, TValue<String>::from_value_assumed(input.value()),
                    codec);
            else
                return decode_bytes(
                    thread, TValue<Bytes>::from_value_assumed(input.value()),
                    codec);
        }
    }  // namespace

    Value encode_string(ThreadState *thread, TValue<String> input,
                        ClCodecEncoding encoding)
    {
        Owned<TValue<String>> source(input);
        const size_t size = source.extract()->count.extract();
        std::array<char32_t, kInputWords> words;
        size_t required = 0;
        for(size_t offset = 0; offset < size;)
        {
            size_t count = std::min(kInputWords, size - offset);
            std::memcpy(words.data(), source.extract()->data + offset,
                        count * sizeof(char32_t));
            auto measured =
                cl_codec_measure_encode(encoding, words.data(), count);
            if(measured.status == CL_CODEC_INVALID_INPUT)
                return encode_error(thread, source.value(), encoding,
                                    offset + measured.input_consumed);
            if(measured.status != CL_CODEC_COMPLETE)
                return codec_failure(thread, measured.status);
            if(measured.output_count > size_t(value_smi_max) - required)
                return codec_failure(thread, CL_CODEC_SIZE_OVERFLOW);
            required += measured.output_count;
            offset += count;
        }
        if(required > std::numeric_limits<size_t>::max() - sizeof(Bytes) -
                          value_ptr_granularity)
            return codec_failure(thread, CL_CODEC_SIZE_OVERFLOW);
        Owned<TValue<Bytes>> output(thread->make_object_value<Bytes>(
            TValue<SMI>::from_smi(int64_t(required))));
        CodecEncoder encoder(encoding);
        size_t written = 0;
        for(size_t offset = 0; offset < size;)
        {
            size_t count = std::min(kInputWords, size - offset);
            std::memcpy(words.data(), source.extract()->data + offset,
                        count * sizeof(char32_t));
            auto chunk = encoder.encode({words.data(), count});
            if(chunk.status != CL_CODEC_COMPLETE ||
               (!chunk.input_accepted || chunk.input_accepted > count) ||
               chunk.output.size() > required - written)
                return codec_failure(thread, chunk.status == CL_CODEC_COMPLETE
                                                 ? CL_CODEC_INVALID_ARGUMENT
                                                 : chunk.status);
            if(!chunk.output.empty())
                std::memcpy(output.extract()->data + written,
                            chunk.output.data(), chunk.output.size());
            written += chunk.output.size();
            offset += chunk.input_accepted;
        }
        if(written != required)
            return codec_failure(thread, CL_CODEC_INVALID_ARGUMENT);
        return output.raw_value();
    }

    Value decode_bytes(ThreadState *thread, TValue<Bytes> input,
                       ClCodecEncoding encoding)
    {
        Owned<TValue<Bytes>> source(input);
        size_t size = source.extract()->count.extract();
        auto measured = cl_codec_measure_decode(encoding, {nullptr, 0},
                                                {source.extract()->data, size});
        if(measured.status == CL_CODEC_INVALID_INPUT ||
           measured.status == CL_CODEC_INCOMPLETE_INPUT)
            return raise_unicode_error(
                thread, Direction::Decode, encoding, source.raw_value(),
                measured.input_consumed,
                measured.input_consumed + measured.error_length,
                decode_reason(measured.error));
        if(measured.status != CL_CODEC_COMPLETE)
            return codec_failure(thread, measured.status);
        size_t required = measured.output_count;
        if(required > size_t(value_smi_max) ||
           required > (std::numeric_limits<size_t>::max() - sizeof(String) -
                       value_ptr_granularity) /
                          sizeof(cl_wchar))
            return codec_failure(thread, CL_CODEC_SIZE_OVERFLOW);
        Owned<TValue<String>> output(thread->make_object_value<String>(
            TValue<SMI>::from_smi(int64_t(required))));
        CodecDecoder decoder(encoding);
        size_t offset = 0, written = 0;
        while(offset < size)
        {
            auto chunk =
                decoder.decode({source.extract()->data + offset, size - offset},
                               InputEnd::Final);
            if((chunk.status != CL_CODEC_COMPLETE &&
                chunk.status != CL_CODEC_INCOMPLETE_INPUT) ||
               !chunk.input_accepted || chunk.input_accepted > size - offset ||
               chunk.output.size() > required - written)
                return codec_failure(
                    thread, chunk.status == CL_CODEC_COMPLETE ||
                                    chunk.status == CL_CODEC_INCOMPLETE_INPUT
                                ? CL_CODEC_INVALID_ARGUMENT
                                : chunk.status);
            if(!chunk.output.empty())
                std::memcpy(output.extract()->data + written,
                            chunk.output.data(),
                            chunk.output.size() * sizeof(char32_t));
            written += chunk.output.size();
            offset += chunk.input_accepted;
        }
        if(written != required || !decoder.pending().empty())
            return codec_failure(thread, CL_CODEC_INVALID_ARGUMENT);
        return output.raw_value();
    }

    void install_text_codec_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> defaults(
            active_thread()->make_object_value<Tuple>(2));
        defaults.extract()->initialize_item_unchecked(
            0, vm->get_or_create_interned_string_value(L"utf-8").raw_value());
        defaults.extract()->initialize_item_unchecked(
            1, vm->get_or_create_interned_string_value(L"strict").raw_value());
        static constexpr const wchar_t *names[] = {L"encoding", L"errors"};
        const auto encode =
            with_defaults(with_keyword_parameter_names(
                              builtin_intrinsic_method(
                                  L"encode", native_codec<Direction::Encode>),
                              names, 2, 1),
                          defaults.value());
        const auto decode =
            with_defaults(with_keyword_parameter_names(
                              builtin_intrinsic_method(
                                  L"decode", native_codec<Direction::Decode>),
                              names, 2, 1),
                          defaults.value());
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->str_class(), &encode, 1),
            "installing str.encode");
        unwrap_bootstrap_expected(vm,
                                  install_builtin_intrinsic_methods(
                                      vm, vm->bytes_class(), &decode, 1),
                                  "installing bytes.decode");
    }
}  // namespace cl
