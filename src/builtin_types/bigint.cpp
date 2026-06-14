#include "builtin_types/bigint.h"

#include "runtime/thread_state.h"

#include <cassert>

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

}  // namespace cl
