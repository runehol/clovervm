#ifndef CL_INT_BASE_FORMAT_H
#define CL_INT_BASE_FORMAT_H

#include "builtin_types/bigint.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

namespace cl::int_format_detail
{
    class SmiMagnitude
    {
    public:
        // The negative endpoint has one more magnitude bit than positive SMIs.
        static constexpr size_t max_bits =
            std::numeric_limits<uint64_t>::digits - value_tag_bits;

        explicit SmiMagnitude(int64_t value)
            : magnitude_(value < 0 ? uint64_t(-(value + 1)) + 1
                                   : uint64_t(value)),
              negative_(value < 0)
        {
            assert(value >= value_smi_min && value <= value_smi_max);
        }

        bool negative() const { return negative_; }
        Expected<size_t> bit_length() const
        {
            return Expected<size_t>::ok(std::bit_width(magnitude_));
        }
        template <unsigned DigitBits> unsigned digit(size_t offset) const
        {
            return unsigned((magnitude_ >> offset) & ((1u << DigitBits) - 1));
        }

    private:
        uint64_t magnitude_;
        bool negative_;
    };

    class BigIntMagnitude
    {
    public:
        explicit BigIntMagnitude(ConstBigIntView view) : view_(view)
        {
            assert(is_normalized_bigint_view(view_));
        }

        bool negative() const { return view_.signum < 0; }
        Expected<size_t> bit_length() const
        {
            if(view_.n_digits >
               (std::numeric_limits<size_t>::max() - kDigitBits) / kDigitBits)
            {
                return Expected<size_t>::raise_exception(
                    L"OverflowError", L"int too large to format");
            }
            return Expected<size_t>::ok(bigint_abs_bit_length(view_));
        }
        template <unsigned DigitBits> unsigned digit(size_t offset) const
        {
            const size_t limb = offset / kDigitBits;
            const unsigned shift = unsigned(offset % kDigitBits);
            double_digit_t chunk = view_.digits[limb];
            if constexpr(kDigitBits % DigitBits != 0)
            {
                if(shift + DigitBits > kDigitBits && limb + 1 < view_.n_digits)
                {
                    chunk |= double_digit_t(view_.digits[limb + 1])
                             << kDigitBits;
                }
            }
            return unsigned((chunk >> shift) & ((1u << DigitBits) - 1));
        }

    private:
        ConstBigIntView view_;
    };

    template <size_t Capacity> class FixedBuffer
    {
    public:
        constexpr size_t max_size() const { return Capacity; }
        void resize(size_t size) { assert(size <= Capacity); }
        wchar_t *data() { return data_.data(); }

    private:
        std::array<wchar_t, Capacity> data_;
    };

    class GrowableBuffer
    {
    public:
        size_t max_size() const
        {
            return std::min(size_t(std::numeric_limits<ptrdiff_t>::max()) /
                                sizeof(wchar_t),
                            size_t(value_smi_max));
        }
        void resize(size_t size)
        {
            // The formatter overwrites every character before exposing a view.
            if(size > capacity_)
            {
                data_ = std::make_unique_for_overwrite<wchar_t[]>(size);
                capacity_ = size;
            }
        }
        wchar_t *data() { return data_.get(); }

    private:
        std::unique_ptr<wchar_t[]> data_;
        size_t capacity_ = 0;
    };

    template <unsigned DigitBits>
    inline constexpr size_t max_smi_result_chars =
        (SmiMagnitude::max_bits + DigitBits - 1) / DigitBits + 3;

    // The returned view borrows output until its next mutation/destruction.
    // Read borrowed BigInt limbs before allocating the final VM String.
    template <unsigned DigitBits, typename Integer, typename Buffer>
    [[nodiscard]] Expected<std::wstring_view>
    format_power_of_two(const Integer &value, Buffer &output)
    {
        static_assert(DigitBits == 1 || DigitBits == 3 || DigitBits == 4);
        const size_t bit_length = CL_TRY(value.bit_length());
        const size_t digits =
            bit_length == 0 ? 1 : (bit_length - 1) / DigitBits + 1;
        const size_t prefix_size = value.negative() ? 3 : 2;
        if(output.max_size() < prefix_size ||
           digits > output.max_size() - prefix_size)
        {
            return Expected<std::wstring_view>::raise_exception(
                L"OverflowError", L"int too large to format");
        }
        const size_t size = digits + prefix_size;
        output.resize(size);
        wchar_t *text = output.data();
        size_t cursor = 0;
        if(value.negative())
        {
            text[cursor++] = L'-';
        }
        text[cursor++] = L'0';
        text[cursor++] = DigitBits == 1 ? L'b' : DigitBits == 3 ? L'o' : L'x';
        for(size_t index = digits; index > 0; --index)
        {
            const unsigned digit =
                bit_length == 0
                    ? 0
                    : value.template digit<DigitBits>((index - 1) * DigitBits);
            text[cursor++] = L"0123456789abcdef"[digit];
        }
        return Expected<std::wstring_view>::ok(std::wstring_view(text, size));
    }
}  // namespace cl::int_format_detail

#endif  // CL_INT_BASE_FORMAT_H
