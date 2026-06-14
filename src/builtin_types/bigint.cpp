#include "builtin_types/bigint.h"

#include "runtime/thread_state.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace cl
{
    SmiBigInt::SmiBigInt(int64_t decoded_smi_range_int)
    {
        assert(decoded_smi_range_int >= value_smi_min);
        assert(decoded_smi_range_int <= value_smi_max);

        double_digit_t magnitude;
        if(decoded_smi_range_int < 0)
        {
            signum_ = -1;
            magnitude = double_digit_t(-(decoded_smi_range_int + 1)) + 1;
        }
        else if(decoded_smi_range_int > 0)
        {
            signum_ = 1;
            magnitude = static_cast<double_digit_t>(decoded_smi_range_int);
        }
        else
        {
            signum_ = 0;
            magnitude = 0;
        }

        digits_[0] = static_cast<digit_t>(magnitude);
        digits_[1] = static_cast<digit_t>(magnitude >> kDigitBits);
        if(digits_[1] != 0)
        {
            n_digits_ = 2;
        }
        else if(digits_[0] != 0)
        {
            n_digits_ = 1;
        }
        else
        {
            n_digits_ = 0;
        }
    }

    BigIntScratch::BigIntScratch(size_t capacity)
        : capacity_(capacity), n_digits_(0), signum_(0), digits_(inline_digits_)
    {
        if(capacity_ > kInlineDigits)
        {
            overflow_.resize(capacity_);
            digits_ = overflow_.data();
        }
    }

    MutableBigIntView BigIntScratch::mutable_view()
    {
        return MutableBigIntView{capacity_, n_digits_, signum_, digits_};
    }

    ConstBigIntView BigIntScratch::view() const
    {
        return ConstBigIntView{n_digits_, signum_, digits_};
    }

    ConstBigIntView BigInt::view() const
    {
        return ConstBigIntView{n_digits_, signum_, digits_};
    }

    MutableBigIntView BigInt::mutable_view_for_initialization()
    {
        return MutableBigIntView{storage_count_for(n_digits_), n_digits_,
                                 signum_, digits_};
    }

    BigInt *make_uninitialized_bigint_for_digits(ThreadState *thread,
                                                 size_t n_digits,
                                                 signum_t signum)
    {
        assert((n_digits == 0 && signum == 0) ||
               (n_digits > 0 && (signum == -1 || signum == 1)));
        return thread->make_object_raw<BigInt>(UninitializedBigIntDigitsTag{},
                                               n_digits, signum);
    }

    bool is_normalized_bigint_view(ConstBigIntView view)
    {
        if(view.n_digits == 0)
        {
            return view.signum == 0;
        }
        if(view.signum != -1 && view.signum != 1)
        {
            return false;
        }
        return view.digits[view.n_digits - 1] != 0;
    }

    ConstBigIntView normalize_bigint_view(ConstBigIntView view)
    {
        while(view.n_digits > 0 && view.digits[view.n_digits - 1] == 0)
        {
            --view.n_digits;
        }
        if(view.n_digits == 0)
        {
            view.signum = 0;
            return view;
        }
        assert(view.signum == -1 || view.signum == 1);
        return view;
    }

    static double_digit_t
    magnitude_to_double_digit_unchecked(ConstBigIntView view)
    {
        assert(view.n_digits <= 2);
        double_digit_t magnitude = view.n_digits > 0 ? view.digits[0] : 0;
        if(view.n_digits > 1)
        {
            magnitude |= double_digit_t(view.digits[1]) << kDigitBits;
        }
        return magnitude;
    }

    static Optional<TValue<SMI>>
    normalized_bigint_view_to_smi_if_fits(ConstBigIntView view)
    {
        assert(is_normalized_bigint_view(view));
        assert(view.signum == 0 || view.signum == -1 || view.signum == 1);
        if(view.signum == 0)
        {
            return Optional<TValue<SMI>>::some(TValue<SMI>::from_smi(0));
        }
        if(view.n_digits > 2)
        {
            return Optional<TValue<SMI>>::none();
        }

        double_digit_t magnitude = magnitude_to_double_digit_unchecked(view);
        int64_t result;
        if(view.signum > 0)
        {
            if(magnitude > static_cast<double_digit_t>(value_smi_max))
            {
                return Optional<TValue<SMI>>::none();
            }
            result = static_cast<int64_t>(magnitude);
        }
        else
        {
            if(magnitude > double_digit_t(-(value_smi_min + 1)) + 1)
            {
                return Optional<TValue<SMI>>::none();
            }
            result = -static_cast<int64_t>(magnitude);
        }
        return Optional<TValue<SMI>>::some(TValue<SMI>::from_smi(result));
    }

    Expected<TValue<SMI>> bigint_to_smi(ConstBigIntView view)
    {
        assert(is_normalized_bigint_view(view));
        Optional<TValue<SMI>> result =
            normalized_bigint_view_to_smi_if_fits(view);
        if(result.has_value())
        {
            return Expected<TValue<SMI>>::ok(result.value());
        }
        return Expected<TValue<SMI>>::raise_exception(L"OverflowError",
                                                      L"integer overflow");
    }

    Expected<Value> finalize_bigint(ThreadState *thread, ConstBigIntView view)
    {
        assert(is_normalized_bigint_view(view));
        Optional<TValue<SMI>> smi = normalized_bigint_view_to_smi_if_fits(view);
        if(smi.has_value())
        {
            return Expected<Value>::ok(smi.value().raw_value());
        }

        BigInt *bigint = make_uninitialized_bigint_for_digits(
            thread, view.n_digits, view.signum);
        MutableBigIntView dest = bigint->mutable_view_for_initialization();
        assert(dest.capacity >= view.n_digits);
        std::memcpy(dest.digits, view.digits, view.n_digits * sizeof(digit_t));
        return Expected<Value>::ok(Value::from_oop(bigint));
    }

    Expected<Value> bigint_from_int64(ThreadState *thread, int64_t value)
    {
        digit_t digits[2];
        signum_t signum;
        double_digit_t magnitude;
        if(value < 0)
        {
            signum = -1;
            magnitude = double_digit_t(-(value + 1)) + 1;
        }
        else if(value > 0)
        {
            signum = 1;
            magnitude = static_cast<double_digit_t>(value);
        }
        else
        {
            signum = 0;
            magnitude = 0;
        }

        digits[0] = static_cast<digit_t>(magnitude);
        digits[1] = static_cast<digit_t>(magnitude >> kDigitBits);
        size_t n_digits = digits[1] != 0 ? 2 : (digits[0] != 0 ? 1 : 0);
        return finalize_bigint(thread,
                               ConstBigIntView{n_digits, signum, digits});
    }

    Expected<int64_t> bigint_to_int64(ConstBigIntView view)
    {
        assert(is_normalized_bigint_view(view));
        if(view.signum == 0)
        {
            return Expected<int64_t>::ok(0);
        }
        if(view.n_digits > 2)
        {
            return Expected<int64_t>::raise_exception(L"OverflowError",
                                                      L"integer overflow");
        }

        double_digit_t magnitude = magnitude_to_double_digit_unchecked(view);
        if(view.signum > 0)
        {
            if(magnitude > double_digit_t(std::numeric_limits<int64_t>::max()))
            {
                return Expected<int64_t>::raise_exception(L"OverflowError",
                                                          L"integer overflow");
            }
            return Expected<int64_t>::ok(static_cast<int64_t>(magnitude));
        }

        constexpr double_digit_t min_magnitude =
            double_digit_t(std::numeric_limits<int64_t>::max()) + 1;
        if(magnitude > min_magnitude)
        {
            return Expected<int64_t>::raise_exception(L"OverflowError",
                                                      L"integer overflow");
        }
        if(magnitude == min_magnitude)
        {
            return Expected<int64_t>::ok(std::numeric_limits<int64_t>::min());
        }
        return Expected<int64_t>::ok(-static_cast<int64_t>(magnitude));
    }

    Expected<Value> bigint_negate(ThreadState *thread, ConstBigIntView view)
    {
        assert(is_normalized_bigint_view(view));
        ConstBigIntView negated{
            view.n_digits, static_cast<signum_t>(-view.signum), view.digits};
        return finalize_bigint(thread, negated);
    }

    static void set_zero(MutableBigIntView *dest)
    {
        dest->n_digits = 0;
        dest->signum = 0;
    }

    static void copy_bigint_view(MutableBigIntView *dest, ConstBigIntView src)
    {
        assert(is_normalized_bigint_view(src));
        assert(dest->capacity >= src.n_digits);
        assert(dest->digits != src.digits);
        if(src.n_digits > 0)
        {
            std::memcpy(dest->digits, src.digits,
                        src.n_digits * sizeof(digit_t));
        }
        dest->n_digits = src.n_digits;
        dest->signum = src.signum;
    }

    void bigint_abs_add(MutableBigIntView *dest, ConstBigIntView left,
                        ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left));
        assert(is_normalized_bigint_view(right));
        size_t max_digits = std::max(left.n_digits, right.n_digits);
        assert(dest->capacity >= max_digits + 1);
        assert(dest->digits != left.digits);
        assert(dest->digits != right.digits);

        double_digit_t carry = 0;
        for(size_t idx = 0; idx < max_digits; ++idx)
        {
            double_digit_t left_digit =
                idx < left.n_digits ? left.digits[idx] : 0;
            double_digit_t right_digit =
                idx < right.n_digits ? right.digits[idx] : 0;
            double_digit_t sum = left_digit + right_digit + carry;
            dest->digits[idx] = static_cast<digit_t>(sum);
            carry = sum >> kDigitBits;
        }

        dest->n_digits = max_digits;
        if(carry != 0)
        {
            dest->digits[dest->n_digits] = static_cast<digit_t>(carry);
            ++dest->n_digits;
        }
        dest->signum = dest->n_digits == 0 ? 0 : 1;
        assert(is_normalized_bigint_view(dest->view()));
    }

    void bigint_abs_sub(MutableBigIntView *dest, ConstBigIntView left,
                        ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left));
        assert(is_normalized_bigint_view(right));
        assert(compare_bigint_abs(left, right) >= 0);
        assert(dest->capacity >= left.n_digits);
        assert(dest->digits != left.digits);
        assert(dest->digits != right.digits);

        double_digit_t borrow = 0;
        for(size_t idx = 0; idx < left.n_digits; ++idx)
        {
            double_digit_t left_digit = left.digits[idx];
            double_digit_t right_digit =
                idx < right.n_digits ? right.digits[idx] : 0;
            double_digit_t subtrahend = right_digit + borrow;
            if(left_digit < subtrahend)
            {
                dest->digits[idx] =
                    static_cast<digit_t>(kDigitBase + left_digit - subtrahend);
                borrow = 1;
            }
            else
            {
                dest->digits[idx] =
                    static_cast<digit_t>(left_digit - subtrahend);
                borrow = 0;
            }
        }
        assert(borrow == 0);

        dest->n_digits = left.n_digits;
        dest->signum = dest->n_digits == 0 ? 0 : 1;
        ConstBigIntView normalized = normalize_bigint_view(dest->view());
        dest->n_digits = normalized.n_digits;
        dest->signum = normalized.signum;
    }

    void bigint_add(MutableBigIntView *dest, ConstBigIntView left,
                    ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left));
        assert(is_normalized_bigint_view(right));
        assert(dest->digits != left.digits);
        assert(dest->digits != right.digits);

        if(left.signum == 0)
        {
            copy_bigint_view(dest, right);
            assert(is_normalized_bigint_view(dest->view()));
            return;
        }
        if(right.signum == 0)
        {
            copy_bigint_view(dest, left);
            assert(is_normalized_bigint_view(dest->view()));
            return;
        }
        if(left.signum == right.signum)
        {
            bigint_abs_add(dest, left, right);
            dest->signum = left.signum;
            assert(is_normalized_bigint_view(dest->view()));
            return;
        }

        int abs_compare = compare_bigint_abs(left, right);
        if(abs_compare == 0)
        {
            set_zero(dest);
        }
        else if(abs_compare > 0)
        {
            bigint_abs_sub(dest, left, right);
            dest->signum = left.signum;
        }
        else
        {
            bigint_abs_sub(dest, right, left);
            dest->signum = right.signum;
        }
        assert(is_normalized_bigint_view(dest->view()));
    }

    void bigint_sub(MutableBigIntView *dest, ConstBigIntView left,
                    ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(right));
        ConstBigIntView negated_right{
            right.n_digits, static_cast<signum_t>(-right.signum), right.digits};
        bigint_add(dest, left, negated_right);
    }

    void bigint_mul(MutableBigIntView *dest, ConstBigIntView left,
                    ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left));
        assert(is_normalized_bigint_view(right));
        assert(dest->digits != left.digits);
        assert(dest->digits != right.digits);

        if(left.signum == 0 || right.signum == 0)
        {
            set_zero(dest);
            return;
        }

        size_t result_capacity = left.n_digits + right.n_digits;
        assert(dest->capacity >= result_capacity);
        std::fill_n(dest->digits, result_capacity, digit_t{0});

        for(size_t left_idx = 0; left_idx < left.n_digits; ++left_idx)
        {
            double_digit_t carry = 0;
            for(size_t right_idx = 0; right_idx < right.n_digits; ++right_idx)
            {
                size_t dest_idx = left_idx + right_idx;
                double_digit_t product = double_digit_t(left.digits[left_idx]) *
                                             right.digits[right_idx] +
                                         dest->digits[dest_idx] + carry;
                dest->digits[dest_idx] = static_cast<digit_t>(product);
                carry = product >> kDigitBits;
            }
            dest->digits[left_idx + right.n_digits] =
                static_cast<digit_t>(carry);
        }

        dest->n_digits = result_capacity;
        dest->signum = left.signum * right.signum;
        ConstBigIntView normalized = normalize_bigint_view(dest->view());
        dest->n_digits = normalized.n_digits;
        dest->signum = normalized.signum;
    }

    void bigint_abs_mul_add_u32(MutableBigIntView *dest, ConstBigIntView src,
                                uint32_t multiplier, uint32_t addend)
    {
        assert(is_normalized_bigint_view(src));
        assert(src.signum == 0 || src.signum == 1);
        assert(multiplier != 0);
        assert(dest->capacity >= src.n_digits + 1);
        assert(dest->digits != src.digits);

        double_digit_t carry = addend;
        for(size_t idx = 0; idx < src.n_digits; ++idx)
        {
            double_digit_t product =
                double_digit_t(src.digits[idx]) * multiplier + carry;
            dest->digits[idx] = static_cast<digit_t>(product);
            carry = product >> kDigitBits;
        }

        dest->n_digits = src.n_digits;
        if(carry != 0)
        {
            dest->digits[dest->n_digits] = static_cast<digit_t>(carry);
            ++dest->n_digits;
        }
        dest->signum = dest->n_digits == 0 ? 0 : 1;
        assert(is_normalized_bigint_view(dest->view()));
    }

    int compare_bigint_abs(ConstBigIntView left, ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left));
        assert(is_normalized_bigint_view(right));
        if(left.n_digits != right.n_digits)
        {
            return left.n_digits < right.n_digits ? -1 : 1;
        }
        for(size_t idx = left.n_digits; idx > 0; --idx)
        {
            digit_t left_digit = left.digits[idx - 1];
            digit_t right_digit = right.digits[idx - 1];
            if(left_digit != right_digit)
            {
                return left_digit < right_digit ? -1 : 1;
            }
        }
        return 0;
    }

    int compare_bigint(ConstBigIntView left, ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left));
        assert(is_normalized_bigint_view(right));
        if(left.signum != right.signum)
        {
            return left.signum < right.signum ? -1 : 1;
        }
        if(left.signum == 0)
        {
            return 0;
        }

        int abs_compare = compare_bigint_abs(left, right);
        return left.signum > 0 ? abs_compare : -abs_compare;
    }

    static uint32_t divmod_abs_by_u32(MutableBigIntView *quotient,
                                      ConstBigIntView dividend,
                                      uint32_t divisor)
    {
        assert(is_normalized_bigint_view(dividend));
        assert(divisor != 0);
        assert(dividend.signum == 0 || dividend.signum == 1);
        assert(quotient->capacity >= dividend.n_digits);
        assert(quotient->digits != dividend.digits);

        double_digit_t remainder = 0;
        for(size_t idx = dividend.n_digits; idx > 0; --idx)
        {
            double_digit_t accumulator =
                (remainder << kDigitBits) | dividend.digits[idx - 1];
            quotient->digits[idx - 1] =
                static_cast<digit_t>(accumulator / divisor);
            remainder = accumulator % divisor;
        }

        quotient->n_digits = dividend.n_digits;
        quotient->signum = quotient->n_digits == 0 ? 0 : 1;
        ConstBigIntView normalized = normalize_bigint_view(quotient->view());
        quotient->n_digits = normalized.n_digits;
        quotient->signum = normalized.n_digits == 0 ? 0 : 1;
        return static_cast<uint32_t>(remainder);
    }

    std::wstring bigint_to_decimal_string(ConstBigIntView view)
    {
        static constexpr uint32_t kDecimalBase = 1000000000;
        static constexpr uint32_t kDecimalBaseDigits = 9;

        assert(is_normalized_bigint_view(view));
        if(view.signum == 0)
        {
            return L"0";
        }

        ConstBigIntView current{view.n_digits, 1, view.digits};
        BigIntScratch scratch0(view.n_digits);
        BigIntScratch scratch1(view.n_digits);
        bool use_first_scratch = true;
        std::vector<uint32_t> chunks;
        chunks.reserve(size_t(view.n_digits) * 10 / kDecimalBaseDigits + 1);

        while(current.n_digits > 0)
        {
            MutableBigIntView quotient = use_first_scratch
                                             ? scratch0.mutable_view()
                                             : scratch1.mutable_view();
            uint32_t remainder =
                divmod_abs_by_u32(&quotient, current, kDecimalBase);
            chunks.push_back(remainder);
            current = quotient.view();
            use_first_scratch = !use_first_scratch;
        }

        std::wstring result;
        result.reserve(chunks.size() * kDecimalBaseDigits +
                       (view.signum < 0 ? 1 : 0));
        if(view.signum < 0)
        {
            result.push_back(L'-');
        }

        result += std::to_wstring(chunks.back());
        for(size_t chunk_index = chunks.size() - 1; chunk_index > 0;
            --chunk_index)
        {
            std::wstring chunk = std::to_wstring(chunks[chunk_index - 1]);
            result.append(kDecimalBaseDigits - chunk.size(), L'0');
            result += chunk;
        }
        return result;
    }

}  // namespace cl
