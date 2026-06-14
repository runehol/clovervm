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
    ConstBigIntView smi_bigint_view(int64_t decoded_smi_range_int,
                                    SmiViewStorage *storage)
    {
        assert(decoded_smi_range_int >= value_smi_min);
        assert(decoded_smi_range_int <= value_smi_max);

        signum_t signum;
        double_digit_t magnitude;
        if(decoded_smi_range_int < 0)
        {
            signum = -1;
            magnitude = double_digit_t(-(decoded_smi_range_int + 1)) + 1;
        }
        else if(decoded_smi_range_int > 0)
        {
            signum = 1;
            magnitude = static_cast<double_digit_t>(decoded_smi_range_int);
        }
        else
        {
            signum = 0;
            magnitude = 0;
        }

        storage->digits[0] = static_cast<digit_t>(magnitude);
        storage->digits[1] = static_cast<digit_t>(magnitude >> kDigitBits);
        size_t n_digits;
        if(storage->digits[1] != 0)
        {
            n_digits = 2;
        }
        else if(storage->digits[0] != 0)
        {
            n_digits = 1;
        }
        else
        {
            n_digits = 0;
        }

        return ConstBigIntView{n_digits, signum, storage->digits};
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

    static void bigint_abs_add_into(MutableBigIntView *dest,
                                    ConstBigIntView left, ConstBigIntView right)
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

    static void bigint_abs_sub_into(MutableBigIntView *dest,
                                    ConstBigIntView left, ConstBigIntView right)
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

    static void bigint_add_into(MutableBigIntView *dest, ConstBigIntView left,
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
            bigint_abs_add_into(dest, left, right);
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
            bigint_abs_sub_into(dest, left, right);
            dest->signum = left.signum;
        }
        else
        {
            bigint_abs_sub_into(dest, right, left);
            dest->signum = right.signum;
        }
        assert(is_normalized_bigint_view(dest->view()));
    }

    static void bigint_sub_into(MutableBigIntView *dest, ConstBigIntView left,
                                ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(right));
        ConstBigIntView negated_right{
            right.n_digits, static_cast<signum_t>(-right.signum), right.digits};
        bigint_add_into(dest, left, negated_right);
    }

    static void bigint_mul_into(MutableBigIntView *dest, ConstBigIntView left,
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

    static void bigint_digits_complement_into(digit_t *dest, const digit_t *src,
                                              size_t n_digits)
    {
        digit_t carry = 1;
        for(size_t idx = 0; idx < n_digits; ++idx)
        {
            double_digit_t sum = double_digit_t(src[idx] ^ ~digit_t{0}) + carry;
            dest[idx] = static_cast<digit_t>(sum);
            carry = static_cast<digit_t>(sum >> kDigitBits);
        }
        assert(carry == 0);
    }

    struct BigIntAndOperation
    {
        static bool result_negative(bool left_negative, bool right_negative)
        {
            return left_negative && right_negative;
        }

        static digit_t apply(digit_t left, digit_t right)
        {
            return left & right;
        }

        static digit_t tail(digit_t larger_digit, bool smaller_negative)
        {
            return smaller_negative ? larger_digit : digit_t{0};
        }
    };

    struct BigIntXorOperation
    {
        static bool result_negative(bool left_negative, bool right_negative)
        {
            return left_negative != right_negative;
        }

        static digit_t apply(digit_t left, digit_t right)
        {
            return left ^ right;
        }

        static digit_t tail(digit_t larger_digit, bool smaller_negative)
        {
            return smaller_negative ? larger_digit ^ ~digit_t{0} : larger_digit;
        }
    };

    struct BigIntOrOperation
    {
        static bool result_negative(bool left_negative, bool right_negative)
        {
            return left_negative || right_negative;
        }

        static digit_t apply(digit_t left, digit_t right)
        {
            return left | right;
        }

        static digit_t tail(digit_t larger_digit, bool smaller_negative)
        {
            return smaller_negative ? ~digit_t{0} : larger_digit;
        }
    };

    template <typename Operation>
    static Expected<Value> bigint_bitwise(ThreadState *thread,
                                          ConstBigIntView left,
                                          ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left));
        assert(is_normalized_bigint_view(right));

        if(left.n_digits < right.n_digits)
        {
            std::swap(left, right);
        }

        bool left_negative = left.signum < 0;
        bool right_negative = right.signum < 0;
        BigIntScratch left_complement_scratch(left.n_digits);
        BigIntScratch right_complement_scratch(right.n_digits);

        if(left_negative)
        {
            MutableBigIntView complemented =
                left_complement_scratch.mutable_view();
            bigint_digits_complement_into(complemented.digits, left.digits,
                                          left.n_digits);
            left = ConstBigIntView{left.n_digits, 1, complemented.digits};
        }
        if(right_negative)
        {
            MutableBigIntView complemented =
                right_complement_scratch.mutable_view();
            bigint_digits_complement_into(complemented.digits, right.digits,
                                          right.n_digits);
            right = ConstBigIntView{right.n_digits, 1, complemented.digits};
        }

        bool result_negative =
            Operation::result_negative(left_negative, right_negative);
        size_t result_digits = left.n_digits;

        if(result_digits == 0)
        {
            return finalize_bigint(thread, ConstBigIntView{0, 0, nullptr});
        }

        BigIntScratch result_scratch(result_digits + (result_negative ? 1 : 0));
        MutableBigIntView result = result_scratch.mutable_view();

        size_t idx = 0;
        for(; idx < right.n_digits && idx < result_digits; ++idx)
        {
            result.digits[idx] =
                Operation::apply(left.digits[idx], right.digits[idx]);
        }

        if(idx < result_digits)
        {
            for(; idx < result_digits; ++idx)
            {
                result.digits[idx] =
                    Operation::tail(left.digits[idx], right_negative);
            }
        }

        result.n_digits = result_digits;
        result.signum = result_digits == 0 ? 0 : 1;
        if(result_negative)
        {
            result.digits[result_digits] = ~digit_t{0};
            bigint_digits_complement_into(result.digits, result.digits,
                                          result_digits + 1);
            result.n_digits = result_digits + 1;
            ConstBigIntView normalized = normalize_bigint_view(result.view());
            result.n_digits = normalized.n_digits;
            result.signum = result.n_digits == 0 ? 0 : -1;
        }
        else
        {
            ConstBigIntView normalized = normalize_bigint_view(result.view());
            result.n_digits = normalized.n_digits;
            result.signum = normalized.signum;
        }

        assert(is_normalized_bigint_view(result.view()));
        return finalize_bigint(thread, result.view());
    }

    static uint32_t divmod_abs_by_u32(MutableBigIntView *quotient,
                                      ConstBigIntView dividend,
                                      uint32_t divisor);

    static uint32_t countl_zero(digit_t value)
    {
        assert(value != 0);
        return static_cast<uint32_t>(__builtin_clz(value));
    }

    static ConstBigIntView abs_bigint_view(ConstBigIntView view)
    {
        assert(is_normalized_bigint_view(view));
        return ConstBigIntView{view.n_digits,
                               view.n_digits == 0 ? signum_t{0} : signum_t{1},
                               view.digits};
    }

    static void bigint_abs_mul_digit_into(MutableBigIntView *dest,
                                          ConstBigIntView left, digit_t right)
    {
        assert(is_normalized_bigint_view(left));
        assert(left.signum == 0 || left.signum == 1);
        assert(dest->capacity >= left.n_digits + 1);
        assert(dest->digits != left.digits);

        if(left.signum == 0 || right == 0)
        {
            set_zero(dest);
            return;
        }

        double_digit_t carry = 0;
        for(size_t idx = 0; idx < left.n_digits; ++idx)
        {
            double_digit_t product =
                double_digit_t(left.digits[idx]) * right + carry;
            dest->digits[idx] = static_cast<digit_t>(product);
            carry = product >> kDigitBits;
        }

        dest->n_digits = left.n_digits;
        if(carry != 0)
        {
            dest->digits[dest->n_digits] = static_cast<digit_t>(carry);
            ++dest->n_digits;
        }
        dest->signum = 1;
        assert(is_normalized_bigint_view(dest->view()));
    }

    static void bigint_abs_sub_alias_left(MutableBigIntView *left,
                                          ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left->view()));
        assert(is_normalized_bigint_view(right));
        assert(left->signum == 0 || left->signum == 1);
        assert(right.signum == 0 || right.signum == 1);
        assert(compare_bigint_abs(left->view(), right) >= 0);

        double_digit_t borrow = 0;
        for(size_t idx = 0; idx < left->n_digits; ++idx)
        {
            double_digit_t left_digit = left->digits[idx];
            double_digit_t right_digit =
                idx < right.n_digits ? right.digits[idx] : 0;
            double_digit_t subtrahend = right_digit + borrow;
            if(left_digit < subtrahend)
            {
                left->digits[idx] =
                    static_cast<digit_t>(kDigitBase + left_digit - subtrahend);
                borrow = 1;
            }
            else
            {
                left->digits[idx] =
                    static_cast<digit_t>(left_digit - subtrahend);
                borrow = 0;
            }
        }
        assert(borrow == 0);

        ConstBigIntView normalized = normalize_bigint_view(left->view());
        left->n_digits = normalized.n_digits;
        left->signum = normalized.signum == 0 ? 0 : 1;
    }

    static void bigint_shift_left_into(MutableBigIntView *dest,
                                       ConstBigIntView src,
                                       uint64_t shift_amount)
    {
        assert(is_normalized_bigint_view(src));
        assert(dest->digits != src.digits);
        if(src.signum == 0)
        {
            set_zero(dest);
            return;
        }

        uint64_t whole_digits64 = shift_amount / kDigitBits;
        assert(whole_digits64 <=
               std::numeric_limits<size_t>::max() - src.n_digits - 1);
        size_t whole_digits = static_cast<size_t>(whole_digits64);
        uint32_t intra_digit_shift =
            static_cast<uint32_t>(shift_amount % kDigitBits);
        assert(dest->capacity >= src.n_digits + whole_digits + 1);

        std::fill_n(dest->digits, whole_digits, digit_t{0});
        dest->n_digits = whole_digits;
        dest->signum = src.signum;

        if(intra_digit_shift == 0)
        {
            std::memcpy(dest->digits + whole_digits, src.digits,
                        src.n_digits * sizeof(digit_t));
            dest->n_digits += src.n_digits;
            return;
        }

        uint32_t inverse_shift = kDigitBits - intra_digit_shift;
        digit_t carry = 0;
        for(size_t idx = 0; idx < src.n_digits; ++idx)
        {
            digit_t digit = src.digits[idx];
            dest->digits[dest->n_digits++] =
                static_cast<digit_t>((digit << intra_digit_shift) | carry);
            carry = digit >> inverse_shift;
        }
        if(carry != 0)
        {
            dest->digits[dest->n_digits++] = carry;
        }
        assert(is_normalized_bigint_view(dest->view()));
    }

    static void bigint_shift_right_abs_into(MutableBigIntView *dest,
                                            ConstBigIntView src,
                                            uint64_t shift_amount)
    {
        assert(is_normalized_bigint_view(src));
        assert(src.signum == 0 || src.signum == 1);
        assert(dest->digits != src.digits);
        if(src.signum == 0)
        {
            set_zero(dest);
            return;
        }

        uint64_t whole_digits64 = shift_amount / kDigitBits;
        if(whole_digits64 >= src.n_digits)
        {
            set_zero(dest);
            return;
        }
        size_t whole_digits = static_cast<size_t>(whole_digits64);
        uint32_t intra_digit_shift =
            static_cast<uint32_t>(shift_amount % kDigitBits);

        dest->n_digits = src.n_digits - whole_digits;
        dest->signum = 1;
        assert(dest->capacity >= dest->n_digits);
        if(intra_digit_shift == 0)
        {
            std::memcpy(dest->digits, src.digits + whole_digits,
                        dest->n_digits * sizeof(digit_t));
        }
        else
        {
            uint32_t inverse_shift = kDigitBits - intra_digit_shift;
            digit_t carry = 0;
            for(size_t out_idx = dest->n_digits; out_idx > 0; --out_idx)
            {
                size_t src_idx = whole_digits + out_idx - 1;
                digit_t digit = src.digits[src_idx];
                dest->digits[out_idx - 1] =
                    static_cast<digit_t>((digit >> intra_digit_shift) | carry);
                carry = digit << inverse_shift;
            }
        }

        ConstBigIntView normalized = normalize_bigint_view(dest->view());
        dest->n_digits = normalized.n_digits;
        dest->signum = normalized.signum == 0 ? 0 : 1;
    }

    static bool bigint_abs_shift_right_loses_nonzero_bits(ConstBigIntView src,
                                                          uint64_t shift_amount)
    {
        assert(is_normalized_bigint_view(src));
        assert(src.signum == 0 || src.signum == 1);
        if(src.signum == 0 || shift_amount == 0)
        {
            return false;
        }

        uint64_t whole_digits64 = shift_amount / kDigitBits;
        if(whole_digits64 >= src.n_digits)
        {
            return true;
        }
        size_t whole_digits = static_cast<size_t>(whole_digits64);
        uint32_t intra_digit_shift =
            static_cast<uint32_t>(shift_amount % kDigitBits);

        for(size_t idx = 0; idx < whole_digits; ++idx)
        {
            if(src.digits[idx] != 0)
            {
                return true;
            }
        }
        if(intra_digit_shift == 0)
        {
            return false;
        }

        digit_t mask = (digit_t{1} << intra_digit_shift) - 1;
        return (src.digits[whole_digits] & mask) != 0;
    }

    static void bigint_abs_divmod_normalized_into(MutableBigIntView *quotient,
                                                  MutableBigIntView *remainder,
                                                  MutableBigIntView dividend,
                                                  ConstBigIntView divisor)
    {
        assert(is_normalized_bigint_view(divisor));
        assert(divisor.signum == 1);
        assert(dividend.signum == 1);
        assert(dividend.n_digits > divisor.n_digits);
        assert((divisor.digits[divisor.n_digits - 1] &
                (digit_t{1} << (kDigitBits - 1))) != 0);

        size_t n = divisor.n_digits;
        size_t m = dividend.n_digits - n - 1;
        digit_t divisor_high = divisor.digits[n - 1];
        digit_t divisor_next = n >= 2 ? divisor.digits[n - 2] : 0;
        BigIntScratch product_scratch(n + 1);
        MutableBigIntView product = product_scratch.mutable_view();

        std::fill_n(quotient->digits, m + 1, digit_t{0});
        static constexpr double_digit_t base = kDigitBase;
        for(size_t reverse_j = m + 1; reverse_j > 0; --reverse_j)
        {
            size_t j = reverse_j - 1;
            double_digit_t high_pair =
                (double_digit_t(dividend.digits[j + n]) << kDigitBits) |
                dividend.digits[j + n - 1];
            double_digit_t q_hat = high_pair / divisor_high;
            double_digit_t r_hat = high_pair % divisor_high;
            double_digit_t u_jn_2 = j + n >= 2 ? dividend.digits[j + n - 2] : 0;

            while(q_hat == base || q_hat * divisor_next > base * r_hat + u_jn_2)
            {
                --q_hat;
                r_hat += divisor_high;
                if(r_hat >= base)
                {
                    break;
                }
            }

            MutableBigIntView dividend_segment{n + 1, n + 1, 1,
                                               dividend.digits + j};
            ConstBigIntView normalized_segment =
                normalize_bigint_view(dividend_segment.view());
            dividend_segment.n_digits = normalized_segment.n_digits;
            dividend_segment.signum = normalized_segment.signum == 0 ? 0 : 1;

            bigint_abs_mul_digit_into(&product, divisor,
                                      static_cast<digit_t>(q_hat));
            while(compare_bigint_abs(dividend_segment.view(), product.view()) <
                  0)
            {
                --q_hat;
                bigint_abs_mul_digit_into(&product, divisor,
                                          static_cast<digit_t>(q_hat));
            }

            quotient->digits[j] = static_cast<digit_t>(q_hat);
            bigint_abs_sub_alias_left(&dividend_segment, product.view());
        }

        quotient->n_digits = m + 1;
        quotient->signum = quotient->n_digits == 0 ? 0 : 1;
        ConstBigIntView normalized_quotient =
            normalize_bigint_view(quotient->view());
        quotient->n_digits = normalized_quotient.n_digits;
        quotient->signum = normalized_quotient.signum == 0 ? 0 : 1;

        ConstBigIntView normalized_remainder =
            normalize_bigint_view(dividend.view());
        assert(remainder->capacity >= normalized_remainder.n_digits);
        if(normalized_remainder.n_digits > 0)
        {
            std::memcpy(remainder->digits, normalized_remainder.digits,
                        normalized_remainder.n_digits * sizeof(digit_t));
        }
        remainder->n_digits = normalized_remainder.n_digits;
        remainder->signum = normalized_remainder.signum == 0 ? 0 : 1;
    }

    static void bigint_abs_divmod_into(MutableBigIntView *quotient,
                                       MutableBigIntView *remainder,
                                       ConstBigIntView dividend,
                                       ConstBigIntView divisor)
    {
        assert(is_normalized_bigint_view(dividend));
        assert(is_normalized_bigint_view(divisor));
        assert(dividend.signum == 0 || dividend.signum == 1);
        assert(divisor.signum == 1);
        assert(quotient->digits != dividend.digits);
        assert(quotient->digits != divisor.digits);
        assert(remainder->digits != dividend.digits);
        assert(remainder->digits != divisor.digits);

        if(dividend.signum == 0 || compare_bigint_abs(dividend, divisor) < 0)
        {
            set_zero(quotient);
            copy_bigint_view(remainder, dividend);
            return;
        }

        if(divisor.n_digits == 1)
        {
            uint32_t rem =
                divmod_abs_by_u32(quotient, dividend, divisor.digits[0]);
            if(rem == 0)
            {
                set_zero(remainder);
            }
            else
            {
                assert(remainder->capacity >= 1);
                remainder->digits[0] = rem;
                remainder->n_digits = 1;
                remainder->signum = 1;
            }
            return;
        }

        uint64_t normalization_shift =
            countl_zero(divisor.digits[divisor.n_digits - 1]);
        BigIntScratch normalized_dividend_scratch(dividend.n_digits + 1);
        BigIntScratch normalized_divisor_scratch(divisor.n_digits + 1);
        BigIntScratch normalized_remainder_scratch(divisor.n_digits + 1);

        MutableBigIntView normalized_dividend =
            normalized_dividend_scratch.mutable_view();
        MutableBigIntView normalized_divisor =
            normalized_divisor_scratch.mutable_view();
        MutableBigIntView normalized_remainder =
            normalized_remainder_scratch.mutable_view();

        bigint_shift_left_into(&normalized_dividend, dividend,
                               normalization_shift);
        bigint_shift_left_into(&normalized_divisor, divisor,
                               normalization_shift);
        if(normalized_dividend.n_digits == dividend.n_digits)
        {
            assert(normalized_dividend.capacity > normalized_dividend.n_digits);
            normalized_dividend.digits[normalized_dividend.n_digits++] = 0;
        }

        bigint_abs_divmod_normalized_into(quotient, &normalized_remainder,
                                          normalized_dividend,
                                          normalized_divisor.view());
        bigint_shift_right_abs_into(remainder, normalized_remainder.view(),
                                    normalization_shift);
    }

    struct BigIntDivModViews
    {
        ConstBigIntView quotient;
        ConstBigIntView remainder;
    };

    static BigIntDivModViews
    bigint_floor_divmod_views(MutableBigIntView *quotient,
                              MutableBigIntView *remainder,
                              MutableBigIntView *adjusted_quotient,
                              MutableBigIntView *adjusted_remainder,
                              ConstBigIntView left, ConstBigIntView right)
    {
        assert(is_normalized_bigint_view(left));
        assert(is_normalized_bigint_view(right));
        assert(right.signum != 0);

        ConstBigIntView left_abs = abs_bigint_view(left);
        ConstBigIntView right_abs = abs_bigint_view(right);
        bigint_abs_divmod_into(quotient, remainder, left_abs, right_abs);

        signum_t quotient_sign = left.signum * right.signum;
        MutableBigIntView *quotient_result = quotient;
        MutableBigIntView *remainder_result = remainder;
        if(remainder->signum != 0 && quotient_sign < 0)
        {
            digit_t one_digit = 1;
            ConstBigIntView one{1, 1, &one_digit};
            bigint_abs_add_into(adjusted_quotient, quotient->view(), one);
            quotient_result = adjusted_quotient;
        }
        if(quotient_result->n_digits != 0)
        {
            quotient_result->signum = quotient_sign;
        }

        if(remainder->signum != 0 && left.signum != right.signum)
        {
            bigint_abs_sub_into(adjusted_remainder, right_abs,
                                remainder->view());
            remainder_result = adjusted_remainder;
        }
        if(remainder_result->n_digits != 0)
        {
            remainder_result->signum = right.signum;
        }

        return BigIntDivModViews{quotient_result->view(),
                                 remainder_result->view()};
    }

    Expected<Value> bigint_add(ThreadState *thread, ConstBigIntView left,
                               ConstBigIntView right)
    {
        BigIntScratch scratch(std::max(left.n_digits, right.n_digits) + 1);
        MutableBigIntView dest = scratch.mutable_view();
        bigint_add_into(&dest, left, right);
        return finalize_bigint(thread, dest.view());
    }

    Expected<Value> bigint_sub(ThreadState *thread, ConstBigIntView left,
                               ConstBigIntView right)
    {
        BigIntScratch scratch(std::max(left.n_digits, right.n_digits) + 1);
        MutableBigIntView dest = scratch.mutable_view();
        bigint_sub_into(&dest, left, right);
        return finalize_bigint(thread, dest.view());
    }

    Expected<Value> bigint_mul(ThreadState *thread, ConstBigIntView left,
                               ConstBigIntView right)
    {
        BigIntScratch scratch(left.n_digits + right.n_digits);
        MutableBigIntView dest = scratch.mutable_view();
        bigint_mul_into(&dest, left, right);
        return finalize_bigint(thread, dest.view());
    }

    Expected<Value> bigint_floor_div(ThreadState *thread, ConstBigIntView left,
                                     ConstBigIntView right)
    {
        if(right.signum == 0)
        {
            return Expected<Value>::raise_exception(L"ZeroDivisionError",
                                                    L"division by zero");
        }

        BigIntScratch quotient_scratch(left.n_digits);
        BigIntScratch remainder_scratch(std::max<size_t>(left.n_digits, 1));
        BigIntScratch adjusted_quotient_scratch(left.n_digits + 1);
        BigIntScratch adjusted_remainder_scratch(right.n_digits);
        MutableBigIntView quotient = quotient_scratch.mutable_view();
        MutableBigIntView remainder = remainder_scratch.mutable_view();
        MutableBigIntView adjusted_quotient =
            adjusted_quotient_scratch.mutable_view();
        MutableBigIntView adjusted_remainder =
            adjusted_remainder_scratch.mutable_view();
        BigIntDivModViews result =
            bigint_floor_divmod_views(&quotient, &remainder, &adjusted_quotient,
                                      &adjusted_remainder, left, right);
        return finalize_bigint(thread, result.quotient);
    }

    Expected<Value> bigint_mod(ThreadState *thread, ConstBigIntView left,
                               ConstBigIntView right)
    {
        if(right.signum == 0)
        {
            return Expected<Value>::raise_exception(L"ZeroDivisionError",
                                                    L"division by zero");
        }

        BigIntScratch quotient_scratch(left.n_digits);
        BigIntScratch remainder_scratch(std::max<size_t>(left.n_digits, 1));
        BigIntScratch adjusted_quotient_scratch(left.n_digits + 1);
        BigIntScratch adjusted_remainder_scratch(right.n_digits);
        MutableBigIntView quotient = quotient_scratch.mutable_view();
        MutableBigIntView remainder = remainder_scratch.mutable_view();
        MutableBigIntView adjusted_quotient =
            adjusted_quotient_scratch.mutable_view();
        MutableBigIntView adjusted_remainder =
            adjusted_remainder_scratch.mutable_view();
        BigIntDivModViews result =
            bigint_floor_divmod_views(&quotient, &remainder, &adjusted_quotient,
                                      &adjusted_remainder, left, right);
        return finalize_bigint(thread, result.remainder);
    }

    Expected<Value> bigint_lshift(ThreadState *thread, ConstBigIntView left,
                                  uint64_t shift_amount)
    {
        assert(is_normalized_bigint_view(left));
        if(left.signum == 0 || shift_amount == 0)
        {
            return finalize_bigint(thread, left);
        }

        uint64_t whole_digits64 = shift_amount / kDigitBits;
        if(whole_digits64 >
           std::numeric_limits<size_t>::max() - left.n_digits - 1)
        {
            return Expected<Value>::raise_exception(L"OverflowError",
                                                    L"integer overflow");
        }
        size_t capacity =
            left.n_digits + static_cast<size_t>(whole_digits64) + 1;
        BigIntScratch scratch(capacity);
        MutableBigIntView dest = scratch.mutable_view();
        bigint_shift_left_into(&dest, left, shift_amount);
        return finalize_bigint(thread, dest.view());
    }

    Expected<Value> bigint_rshift(ThreadState *thread, ConstBigIntView left,
                                  uint64_t shift_amount)
    {
        assert(is_normalized_bigint_view(left));
        if(left.signum == 0 || shift_amount == 0)
        {
            return finalize_bigint(thread, left);
        }

        uint64_t whole_digits64 = shift_amount / kDigitBits;
        size_t shifted_capacity =
            whole_digits64 >= left.n_digits
                ? 1
                : left.n_digits - static_cast<size_t>(whole_digits64) + 1;
        BigIntScratch scratch(shifted_capacity);
        MutableBigIntView dest = scratch.mutable_view();
        ConstBigIntView left_abs = abs_bigint_view(left);
        bigint_shift_right_abs_into(&dest, left_abs, shift_amount);
        if(left.signum < 0)
        {
            if(bigint_abs_shift_right_loses_nonzero_bits(left_abs,
                                                         shift_amount))
            {
                digit_t one_digit = 1;
                ConstBigIntView one{1, 1, &one_digit};
                BigIntScratch adjusted_scratch(
                    std::max<size_t>(dest.n_digits, 1) + 1);
                MutableBigIntView adjusted = adjusted_scratch.mutable_view();
                bigint_abs_add_into(&adjusted, dest.view(), one);
                adjusted.signum = -1;
                return finalize_bigint(thread, adjusted.view());
            }
            if(dest.n_digits != 0)
            {
                dest.signum = -1;
            }
        }
        return finalize_bigint(thread, dest.view());
    }

    Expected<Value> bigint_invert(ThreadState *thread, ConstBigIntView view)
    {
        assert(is_normalized_bigint_view(view));

        digit_t one_digit = 1;
        ConstBigIntView one{1, 1, &one_digit};
        BigIntScratch scratch(std::max<size_t>(view.n_digits, 1) + 1);
        MutableBigIntView dest = scratch.mutable_view();
        bigint_add_into(&dest, view, one);
        if(dest.n_digits != 0)
        {
            dest.signum = static_cast<signum_t>(-dest.signum);
        }
        return finalize_bigint(thread, dest.view());
    }

    Expected<Value> bigint_and(ThreadState *thread, ConstBigIntView left,
                               ConstBigIntView right)
    {
        return bigint_bitwise<BigIntAndOperation>(thread, left, right);
    }

    Expected<Value> bigint_xor(ThreadState *thread, ConstBigIntView left,
                               ConstBigIntView right)
    {
        return bigint_bitwise<BigIntXorOperation>(thread, left, right);
    }

    Expected<Value> bigint_or(ThreadState *thread, ConstBigIntView left,
                              ConstBigIntView right)
    {
        return bigint_bitwise<BigIntOrOperation>(thread, left, right);
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
