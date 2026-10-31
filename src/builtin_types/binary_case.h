#ifndef CL_BINARY_CASE_H
#define CL_BINARY_CASE_H

#include <cstddef>
#include <cstdint>

namespace cl
{
    enum class BinaryCase
    {
        Lower,
        Upper,
        Swapcase,
        Capitalize,
        Title
    };

    template <BinaryCase Mode> constexpr const wchar_t *binary_case_name()
    {
        if constexpr(Mode == BinaryCase::Lower)
            return L"lower";
        if constexpr(Mode == BinaryCase::Upper)
            return L"upper";
        if constexpr(Mode == BinaryCase::Swapcase)
            return L"swapcase";
        if constexpr(Mode == BinaryCase::Capitalize)
            return L"capitalize";
        return L"title";
    }

    // Stored payloads only: allocation, callbacks, mutation and safepoints are
    // forbidden while these pointers are borrowed. Destination has size bytes.
    template <BinaryCase Mode>
    void transform_binary_case(const uint8_t *source, uint8_t *destination,
                               size_t size)
    {
        bool previous_cased = false;
        for(size_t index = 0; index < size; ++index)
        {
            const uint8_t byte = source[index];
            const bool lower = byte >= 'a' && byte <= 'z';
            const bool upper = byte >= 'A' && byte <= 'Z';
            bool to_upper;
            if constexpr(Mode == BinaryCase::Lower)
                to_upper = false;
            else if constexpr(Mode == BinaryCase::Upper)
                to_upper = true;
            else if constexpr(Mode == BinaryCase::Swapcase)
                to_upper = lower;
            else if constexpr(Mode == BinaryCase::Capitalize)
                to_upper = index == 0;
            else
                to_upper = !previous_cased;
            destination[index] = to_upper && lower    ? byte - ('a' - 'A')
                                 : !to_upper && upper ? byte + ('a' - 'A')
                                                      : byte;
            if constexpr(Mode == BinaryCase::Title)
                previous_cased = lower || upper;
        }
    }
}  // namespace cl
#endif  // CL_BINARY_CASE_H
