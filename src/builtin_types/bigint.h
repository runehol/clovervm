#ifndef CL_BIGINT_H
#define CL_BIGINT_H

#include "object_model/class_object.h"
#include "object_model/object.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cl
{
    class ThreadState;

    using digit_t = uint32_t;
    using double_digit_t = uint64_t;
    using signum_t = int16_t;

    static constexpr uint32_t kDigitBits = sizeof(digit_t) * 8;
    static_assert(kDigitBits == 32);
    static constexpr double_digit_t kDigitBase = double_digit_t{1}
                                                 << kDigitBits;

    struct ConstBigIntView
    {
        size_t n_digits;
        signum_t signum;
        const digit_t *digits;
    };

    struct MutableBigIntView
    {
        size_t capacity;
        size_t n_digits;
        signum_t signum;
        digit_t *digits;

        ConstBigIntView view() const
        {
            return ConstBigIntView{n_digits, signum, digits};
        }

        operator ConstBigIntView() const { return view(); }
    };

    static constexpr size_t kSmiViewDigits = 2;

    struct SmiViewStorage
    {
        digit_t digits[kSmiViewDigits];
    };

    class BigIntScratch
    {
    public:
        explicit BigIntScratch(size_t capacity);
        MutableBigIntView mutable_view();
        ConstBigIntView view() const;

    private:
        static constexpr uint32_t kInlineDigits = 8;

        size_t capacity_;
        size_t n_digits_;
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

        BigInt(ClassObject *cls, UninitializedBigIntDigitsTag, size_t n_digits,
               signum_t signum)
            : Object(cls, native_layout), n_digits_(n_digits), signum_(signum)
        {
            assert((n_digits_ == 0 && signum_ == 0) ||
                   (n_digits_ > 0 && (signum_ == -1 || signum_ == 1)));
        }

        size_t n_digits() const { return n_digits_; }
        signum_t signum() const { return signum_; }
        ConstBigIntView view() const;
        MutableBigIntView mutable_view_for_initialization();

        static size_t size_for(size_t n_digits)
        {
            return sizeof(BigInt) +
                   (storage_count_for(n_digits) - 1) * sizeof(digit_t);
        }

        static size_t size_for(ClassObject *, UninitializedBigIntDigitsTag,
                               size_t n_digits, signum_t)
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
        static size_t storage_count_for(size_t n_digits)
        {
            return n_digits == 0 ? 1 : n_digits;
        }

        size_t n_digits_;
        signum_t signum_;
        digit_t digits_[1];
    };

    BigInt *make_uninitialized_bigint_for_digits(ThreadState *thread,
                                                 size_t n_digits,
                                                 signum_t signum);
    ConstBigIntView smi_bigint_view(int64_t decoded_smi_range_int,
                                    SmiViewStorage *storage);
    bool is_normalized_bigint_view(ConstBigIntView view);
    ConstBigIntView normalize_bigint_view(ConstBigIntView view);
    bool bigint_is_odd(ConstBigIntView view);
    [[nodiscard]] Expected<TValue<SMI>> bigint_to_smi(ConstBigIntView view);
    TValue<SMI> bigint_hash(ConstBigIntView view);
    [[nodiscard]] Expected<Value> finalize_bigint(ThreadState *thread,
                                                  ConstBigIntView view);
    [[nodiscard]] Expected<Value> bigint_from_int64(ThreadState *thread,
                                                    int64_t value);
    [[nodiscard]] Expected<int64_t> bigint_to_int64(ConstBigIntView view);
    [[nodiscard]] Expected<double> bigint_to_double(ConstBigIntView view);
    [[nodiscard]] Expected<Value> bigint_negate(ThreadState *thread,
                                                ConstBigIntView view);
    [[nodiscard]] Expected<Value> bigint_add(ThreadState *thread,
                                             ConstBigIntView left,
                                             ConstBigIntView right);
    [[nodiscard]] Expected<Value> bigint_sub(ThreadState *thread,
                                             ConstBigIntView left,
                                             ConstBigIntView right);
    [[nodiscard]] Expected<Value> bigint_mul(ThreadState *thread,
                                             ConstBigIntView left,
                                             ConstBigIntView right);
    [[nodiscard]] Expected<Value>
    bigint_pow_nonnegative(ThreadState *thread, ConstBigIntView base,
                           ConstBigIntView exponent);
    [[nodiscard]] Expected<Value> bigint_modular_pow(ThreadState *thread,
                                                     ConstBigIntView base,
                                                     ConstBigIntView exponent,
                                                     ConstBigIntView modulo);
    [[nodiscard]] Expected<Value> bigint_floor_div(ThreadState *thread,
                                                   ConstBigIntView left,
                                                   ConstBigIntView right);
    [[nodiscard]] Expected<Value> bigint_mod(ThreadState *thread,
                                             ConstBigIntView left,
                                             ConstBigIntView right);
    [[nodiscard]] Expected<Value> bigint_lshift(ThreadState *thread,
                                                ConstBigIntView left,
                                                uint64_t shift_amount);
    [[nodiscard]] Expected<Value> bigint_rshift(ThreadState *thread,
                                                ConstBigIntView left,
                                                uint64_t shift_amount);
    [[nodiscard]] Expected<Value> bigint_invert(ThreadState *thread,
                                                ConstBigIntView view);
    [[nodiscard]] Expected<Value> bigint_and(ThreadState *thread,
                                             ConstBigIntView left,
                                             ConstBigIntView right);
    [[nodiscard]] Expected<Value> bigint_xor(ThreadState *thread,
                                             ConstBigIntView left,
                                             ConstBigIntView right);
    [[nodiscard]] Expected<Value>
    bigint_or(ThreadState *thread, ConstBigIntView left, ConstBigIntView right);
    void bigint_abs_mul_add_u32(MutableBigIntView *dest, ConstBigIntView src,
                                uint32_t multiplier, uint32_t addend);
    int compare_bigint_abs(ConstBigIntView left, ConstBigIntView right);
    int compare_bigint(ConstBigIntView left, ConstBigIntView right);
    std::wstring bigint_to_decimal_string(ConstBigIntView view);

}  // namespace cl

#endif  // CL_BIGINT_H
