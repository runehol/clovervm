#ifndef CL_BINARY_HEX_H
#define CL_BINARY_HEX_H

#include "builtin_types/bytearray.h"
#include "builtin_types/bytes.h"
#include "builtin_types/str.h"
#include "runtime/thread_state.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

namespace cl
{
    // The public length check has already run and may have called Python.
    [[nodiscard]] inline Expected<wchar_t> binary_hex_separator(Value value)
    {
        wchar_t separator;
        if(has_native_layout<String>(value))
        {
            auto text = string_view(TValue<String>::from_value_assumed(value));
            // Match CPython's one-byte storage check for a str subclass
            // reporting length one, even if it stores more code points.
            if(std::any_of(text.begin(), text.end(),
                           [](wchar_t c) { return c > 255; }))
                return Expected<wchar_t>::raise_exception(
                    L"ValueError", L"sep must be ASCII.");
            separator = text.empty() ? 0 : text.front();
        }
        else if(has_native_layout<Bytes>(value))
        {
            auto bytes = bytes_view(TValue<Bytes>::from_value_assumed(value));
            separator = bytes.empty() ? 0 : bytes.front();
        }
        else
            return Expected<wchar_t>::raise_exception(
                L"TypeError", L"sep must be str or bytes.");

        if(separator > 127)
            return Expected<wchar_t>::raise_exception(L"ValueError",
                                                      L"sep must be ASCII.");
        return Expected<wchar_t>::ok(separator);
    }

    template <typename Binary, typename View>
    [[nodiscard]] Value format_binary_hex(ThreadState *thread,
                                          TValue<Binary> receiver, Value sep,
                                          int32_t group, View view)
    {
        wchar_t separator = 0;
        if(sep.is_none())
            group = 0;
        else
            separator = CL_TRY(binary_hex_separator(sep));
        // Widen before negation so the minimum C int is representable.
        const size_t width =
            group < 0 ? size_t(-int64_t(group)) : size_t(group);
        Owned<TValue<Binary>> source(receiver);
        const size_t size = view(source.value()).size();
        const size_t separators = width && size ? (size - 1) / width : 0;
        constexpr size_t limit =
            std::min(size_t(value_smi_max),
                     (std::numeric_limits<size_t>::max() - sizeof(String)) /
                         sizeof(wchar_t));
        if(size > limit / 2 || separators > limit - 2 * size)
            return thread->set_pending_builtin_exception_string(
                L"OverflowError", L"hex result exceeds string storage limit");

        const size_t result_size = 2 * size + separators;
        TValue<String> result = thread->make_object_value<String>(
            TValue<SMI>::from_smi(static_cast<int64_t>(result_size)));
        // Acquire the span only after result allocation. The fill cannot
        // call back into Python or safepoint.
        std::span<const uint8_t> bytes = view(source.value());
        wchar_t *out = result.extract()->data;
        constexpr wchar_t digits[] = L"0123456789abcdef";
        size_t boundary = size;
        if(separators)
            boundary = group > 0 ? size - separators * width : width;
        for(size_t i = 0; i < size; ++i)
        {
            if(i == boundary)
            {
                *out++ = separator;
                boundary += width;
            }
            const uint8_t byte = bytes[i];
            *out++ = digits[byte >> 4];
            *out++ = digits[byte & 15];
        }
        return result.raw_value();
    }
}  // namespace cl

#endif
