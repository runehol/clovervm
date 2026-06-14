#ifndef CL_BIGINT_H
#define CL_BIGINT_H

#include "object_model/class_object.h"
#include "object_model/object.h"
#include "object_model/value.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl
{
    class ThreadState;

    using digit_t = uint32_t;
    using signum_t = int16_t;

    struct ConstBigIntView
    {
        uint32_t n_digits;
        signum_t signum;
        const digit_t *digits;
    };

    struct MutableBigIntView
    {
        uint32_t capacity;
        uint32_t n_digits;
        signum_t signum;
        digit_t *digits;

        ConstBigIntView view() const
        {
            return ConstBigIntView{n_digits, signum, digits};
        }

        operator ConstBigIntView() const { return view(); }
    };

    class SmiBigInt
    {
    public:
        explicit SmiBigInt(int64_t decoded_smi_range_int);

        operator ConstBigIntView() const { return view(); }
        ConstBigIntView view() const
        {
            return ConstBigIntView{n_digits_, signum_, digits_};
        }

    private:
        uint32_t n_digits_;
        signum_t signum_;
        digit_t digits_[2];
    };

    class BigIntScratch
    {
    public:
        explicit BigIntScratch(uint32_t capacity);
        MutableBigIntView mutable_view();
        ConstBigIntView view() const;

    private:
        static constexpr uint32_t kInlineDigits = 8;

        uint32_t capacity_;
        uint32_t n_digits_;
        signum_t signum_;
        digit_t *digits_;
        digit_t inline_digits_[kInlineDigits];
        std::vector<digit_t> overflow_;
    };

    struct UninitializedBigIntDigitsTag
    {
    };

    class BigInt : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::BigInt;

        BigInt(ClassObject *cls, UninitializedBigIntDigitsTag,
               uint32_t n_digits, signum_t signum)
            : Object(cls, native_layout), n_digits_(n_digits), signum_(signum)
        {
            assert((n_digits_ == 0 && signum_ == 0) ||
                   (n_digits_ > 0 && (signum_ == -1 || signum_ == 1)));
        }

        uint32_t n_digits() const { return n_digits_; }
        signum_t signum() const { return signum_; }
        ConstBigIntView view() const;
        MutableBigIntView mutable_view_for_initialization();

        static size_t size_for(uint32_t n_digits)
        {
            return sizeof(BigInt) +
                   (storage_count_for(n_digits) - 1) * sizeof(digit_t);
        }

        static size_t size_for(ClassObject *, UninitializedBigIntDigitsTag,
                               uint32_t n_digits, signum_t)
        {
            return size_for(n_digits);
        }

        static size_t object_size_in_bytes(const BigInt *bigint)
        {
            return size_for(bigint->n_digits_);
        }

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(BigInt, Object, 0);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(BigInt, BigInt::object_size_in_bytes);

    private:
        static size_t storage_count_for(uint32_t n_digits)
        {
            return n_digits == 0 ? 1 : n_digits;
        }

        uint32_t n_digits_;
        signum_t signum_;
        digit_t digits_[1];
    };

    BigInt *make_uninitialized_bigint_for_digits(ThreadState *thread,
                                                 uint32_t n_digits,
                                                 signum_t signum);

}  // namespace cl

#endif  // CL_BIGINT_H
