#include "builtin_types/bigint.h"

#include "runtime/thread_state.h"

#include <cassert>
#include <cstring>
#include <limits>

namespace cl
{
    SmiBigInt::SmiBigInt(int64_t decoded_smi_range_int)
    {
        assert(decoded_smi_range_int >= value_smi_min);
        assert(decoded_smi_range_int <= value_smi_max);

        uint64_t magnitude;
        if(decoded_smi_range_int < 0)
        {
            signum_ = -1;
            magnitude = uint64_t(-(decoded_smi_range_int + 1)) + 1;
        }
        else if(decoded_smi_range_int > 0)
        {
            signum_ = 1;
            magnitude = static_cast<uint64_t>(decoded_smi_range_int);
        }
        else
        {
            signum_ = 0;
            magnitude = 0;
        }

        digits_[0] = static_cast<digit_t>(magnitude);
        digits_[1] = static_cast<digit_t>(magnitude >> 32);
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

    BigIntScratch::BigIntScratch(uint32_t capacity)
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
        return MutableBigIntView{
            static_cast<uint32_t>(storage_count_for(n_digits_)), n_digits_,
            signum_, digits_};
    }

    BigInt *make_uninitialized_bigint_for_digits(ThreadState *thread,
                                                 uint32_t n_digits,
                                                 signum_t signum)
    {
        assert((n_digits == 0 && signum == 0) ||
               (n_digits > 0 && (signum == -1 || signum == 1)));
        return thread->make_object_raw<BigInt>(UninitializedBigIntDigitsTag{},
                                               n_digits, signum);
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

    static uint64_t magnitude_to_uint64_unchecked(ConstBigIntView view)
    {
        assert(view.n_digits <= 2);
        uint64_t magnitude = view.n_digits > 0 ? view.digits[0] : 0;
        if(view.n_digits > 1)
        {
            magnitude |= uint64_t(view.digits[1]) << 32;
        }
        return magnitude;
    }

    static Optional<TValue<SMI>>
    normalized_bigint_view_to_smi_if_fits(ConstBigIntView view)
    {
        assert(view.signum == 0 || view.signum == -1 || view.signum == 1);
        if(view.signum == 0)
        {
            return Optional<TValue<SMI>>::some(TValue<SMI>::from_smi(0));
        }
        if(view.n_digits > 2)
        {
            return Optional<TValue<SMI>>::none();
        }

        uint64_t magnitude = magnitude_to_uint64_unchecked(view);
        int64_t result;
        if(view.signum > 0)
        {
            if(magnitude > static_cast<uint64_t>(value_smi_max))
            {
                return Optional<TValue<SMI>>::none();
            }
            result = static_cast<int64_t>(magnitude);
        }
        else
        {
            if(magnitude > uint64_t(-(value_smi_min + 1)) + 1)
            {
                return Optional<TValue<SMI>>::none();
            }
            result = -static_cast<int64_t>(magnitude);
        }
        return Optional<TValue<SMI>>::some(TValue<SMI>::from_smi(result));
    }

    Expected<TValue<SMI>> bigint_to_smi(ConstBigIntView view)
    {
        ConstBigIntView normalized = normalize_bigint_view(view);
        Optional<TValue<SMI>> result =
            normalized_bigint_view_to_smi_if_fits(normalized);
        if(result.has_value())
        {
            return Expected<TValue<SMI>>::ok(result.value());
        }
        return Expected<TValue<SMI>>::raise_exception(L"OverflowError",
                                                      L"integer overflow");
    }

    Expected<Value> finalize_bigint(ThreadState *thread, ConstBigIntView view)
    {
        ConstBigIntView normalized = normalize_bigint_view(view);
        Optional<TValue<SMI>> smi =
            normalized_bigint_view_to_smi_if_fits(normalized);
        if(smi.has_value())
        {
            return Expected<Value>::ok(smi.value().raw_value());
        }

        BigInt *bigint = make_uninitialized_bigint_for_digits(
            thread, normalized.n_digits, normalized.signum);
        MutableBigIntView dest = bigint->mutable_view_for_initialization();
        assert(dest.capacity >= normalized.n_digits);
        std::memcpy(dest.digits, normalized.digits,
                    normalized.n_digits * sizeof(digit_t));
        return Expected<Value>::ok(Value::from_oop(bigint));
    }

    Expected<Value> bigint_from_int64(ThreadState *thread, int64_t value)
    {
        digit_t digits[2];
        signum_t signum;
        uint64_t magnitude;
        if(value < 0)
        {
            signum = -1;
            magnitude = uint64_t(-(value + 1)) + 1;
        }
        else if(value > 0)
        {
            signum = 1;
            magnitude = static_cast<uint64_t>(value);
        }
        else
        {
            signum = 0;
            magnitude = 0;
        }

        digits[0] = static_cast<digit_t>(magnitude);
        digits[1] = static_cast<digit_t>(magnitude >> 32);
        uint32_t n_digits = digits[1] != 0 ? 2 : (digits[0] != 0 ? 1 : 0);
        return finalize_bigint(thread,
                               ConstBigIntView{n_digits, signum, digits});
    }

    Expected<int64_t> bigint_to_int64(ConstBigIntView view)
    {
        ConstBigIntView normalized = normalize_bigint_view(view);
        if(normalized.signum == 0)
        {
            return Expected<int64_t>::ok(0);
        }
        if(normalized.n_digits > 2)
        {
            return Expected<int64_t>::raise_exception(L"OverflowError",
                                                      L"integer overflow");
        }

        uint64_t magnitude = magnitude_to_uint64_unchecked(normalized);
        if(normalized.signum > 0)
        {
            if(magnitude > uint64_t(std::numeric_limits<int64_t>::max()))
            {
                return Expected<int64_t>::raise_exception(L"OverflowError",
                                                          L"integer overflow");
            }
            return Expected<int64_t>::ok(static_cast<int64_t>(magnitude));
        }

        constexpr uint64_t min_magnitude =
            uint64_t(std::numeric_limits<int64_t>::max()) + 1;
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

}  // namespace cl
