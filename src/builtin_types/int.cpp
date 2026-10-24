#include "builtin_types/int.h"

#include "builtin_types/bytes.h"
#include "builtin_types/float.h"
#include "builtin_types/hash.h"
#include "builtin_types/num_parse.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/owned.h"
#include "object_model/value.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <bit>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace cl
{
    static constexpr int64_t int64_value_bits =
        std::numeric_limits<int64_t>::digits;
    static constexpr int64_t int64_storage_bits =
        std::numeric_limits<uint64_t>::digits;

    static int64_t count_left_shift_headroom_bits(int64_t value)
    {
        uint64_t signless_bits = value < 0 ? ~static_cast<uint64_t>(value)
                                           : static_cast<uint64_t>(value);
        return static_cast<int64_t>(std::countl_zero(signless_bits) - 1);
    }

    static bool is_smi_or_bool_value(Value value)
    {
        return (value.as.integer & value_not_smi_or_boolean_mask) == 0;
    }

    static int64_t smi_or_bool_as_int(Value value)
    {
        assert(is_smi_or_bool_value(value));
        Value integer_value;
        integer_value.as.integer =
            value.as.integer & value_boolean_to_integer_mask;
        return integer_value.get_smi();
    }

    bool is_intlike_value(Value value)
    {
        return is_smi_or_bool_value(value) || can_convert_to<BigInt>(value);
    }

    Expected<int64_t> validate_length_result(Value value)
    {
        int64_t length;
        if(is_smi_or_bool_value(value))
        {
            length = smi_or_bool_as_int(value);
        }
        else if(can_convert_to<BigInt>(value))
        {
            ConstBigIntView view = value.get_ptr<BigInt>()->view();
            if(view.signum < 0)
            {
                return Expected<int64_t>::raise_exception(
                    L"ValueError", L"__len__() should return >= 0");
            }
            length = CL_TRY(bigint_to_int64(view));
        }
        else
        {
            return Expected<int64_t>::raise_exception(
                L"TypeError", L"__len__() should return an integer");
        }
        if(length < 0)
        {
            return Expected<int64_t>::raise_exception(
                L"ValueError", L"__len__() should return >= 0");
        }
        return Expected<int64_t>::ok(length);
    }

    bool is_exact_int_value(Value value)
    {
        return value.is_smi() || can_convert_to<BigInt>(value);
    }

    NOINLINE Expected<TValue<SMI>>
    try_intlike_value_to_smi_slow(Value value,
                                  const wchar_t *type_error_message)
    {
        if(value.is_bool())
        {
            return Expected<TValue<SMI>>::ok(
                TValue<SMI>::from_smi(smi_or_bool_as_int(value)));
        }
        if(can_convert_to<BigInt>(value))
        {
            return bigint_to_smi(value.get_ptr<BigInt>()->view());
        }
        return Expected<TValue<SMI>>::raise_exception(L"TypeError",
                                                      type_error_message);
    }

    Expected<IntToSmiStatus> try_exact_int_value_to_smi(Value value,
                                                        TValue<SMI> *out)
    {
        if(value.is_smi())
        {
            *out = TValue<SMI>::from_smi(value.get_smi());
            return Expected<IntToSmiStatus>::ok(IntToSmiStatus::Converted);
        }
        if(can_convert_to<BigInt>(value))
        {
            Expected<TValue<SMI>> smi =
                bigint_to_smi(value.get_ptr<BigInt>()->view());
            if(smi.has_exception())
            {
                return Expected<IntToSmiStatus>::propagate_exception();
            }
            *out = smi.value();
            return Expected<IntToSmiStatus>::ok(IntToSmiStatus::Converted);
        }
        return Expected<IntToSmiStatus>::ok(IntToSmiStatus::NotInt);
    }

    ConstBigIntView intlike_value_bigint_view(Value value,
                                              SmiViewStorage *smi_storage)
    {
        if(is_smi_or_bool_value(value))
        {
            return smi_bigint_view(smi_or_bool_as_int(value), smi_storage);
        }
        assert(can_convert_to<BigInt>(value));
        return value.get_ptr<BigInt>()->view();
    }

    ConstBigIntView exact_int_value_bigint_view(Value value,
                                                SmiViewStorage *smi_storage)
    {
        if(value.is_smi())
        {
            return smi_bigint_view(value.get_smi(), smi_storage);
        }
        assert(can_convert_to<BigInt>(value));
        return value.get_ptr<BigInt>()->view();
    }

    static Value parse_int_string(ThreadState *thread, TValue<String> string)
    {
        String *str = string.extract();
        Expected<Value> result = parse_int_string_view(
            thread, std::wstring_view(str->data, size_t(str->count.extract())));
        if(result.has_exception())
        {
            return Value::exception_marker();
        }
        return result.value();
    }

    [[nodiscard]] NOINLINE static Value
    int_conversion_type_error(ThreadState *thread, Value obj)
    {
        TValue<String> name = thread->class_of_value(obj)->get_name();
        std::wstring message = L"int() argument must be a string, a bytes-like "
                               L"object or a real number, not '";
        message.append(string_view(name));
        message += L"'";
        return thread->set_pending_builtin_exception_string(L"TypeError",
                                                            message.c_str());
    }

    [[nodiscard]] static Value convert_to_exact_int(ThreadState *thread,
                                                    Value obj)
    {
        if((obj.as.integer & value_not_smi_or_boolean_mask) == 0)
        {
            Value result;
            result.as.integer = obj.as.integer & value_boolean_to_integer_mask;
            return result;
        }

        if(can_convert_to<BigInt>(obj))
        {
            BigInt *integer = obj.get_ptr<BigInt>();
            if(integer->get_shape()->get_class() ==
               thread->get_machine()->int_class())
            {
                return obj;
            }
            return CL_TRY(finalize_bigint(thread, integer->view()));
        }

        if(can_convert_to<Float>(obj))
        {
            return CL_TRY(
                int_value_from_double(thread, obj.get_ptr<Float>()->value()));
        }
        if(can_convert_to<String>(obj))
        {
            return parse_int_string(thread,
                                    TValue<String>::from_value_assumed(obj));
        }
        if(can_convert_to<Bytes>(obj))
        {
            auto bytes = bytes_view(TValue<Bytes>::from_value_assumed(obj));
            return CL_TRY(parse_int_string_view(
                thread,
                std::string_view(reinterpret_cast<const char *>(bytes.data()),
                                 bytes.size())));
        }

        return int_conversion_type_error(thread, obj);
    }

    [[nodiscard]] NOINLINE static Value
    int_new_subclass_slow(ThreadState *thread, Value cls_value, Value obj)
    {
        VirtualMachine *vm = thread->get_machine();
        if(!can_convert_to<ClassObject>(cls_value))
        {
            TValue<String> name = thread->class_of_value(cls_value)->get_name();
            std::wstring message = L"int.__new__(X): X is not a type object (";
            message.append(name.extract()->data,
                           size_t(name.extract()->count.extract()));
            message += L")";
            return thread->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        TValue<ClassObject> cls =
            TValue<ClassObject>::from_value_assumed(cls_value);
        if(!is_subclass_of(cls.extract(), vm->int_class()))
        {
            TValue<String> name = cls.extract()->get_name();
            std::wstring class_name(name.extract()->data,
                                    size_t(name.extract()->count.extract()));
            std::wstring message = L"int.__new__(" + class_name + L"): " +
                                   class_name + L" is not a subtype of int";
            return thread->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        if(cls.extract()->instance_native_layout_id() != NativeLayoutId::BigInt)
        {
            TValue<String> name = cls.extract()->get_name();
            std::wstring class_name(name.extract()->data,
                                    size_t(name.extract()->count.extract()));
            std::wstring message = L"int.__new__(" + class_name +
                                   L") is not safe, use " + class_name +
                                   L".__new__()";
            return thread->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        Value converted_value = convert_to_exact_int(thread, obj);
        CL_PROPAGATE_EXCEPTION(converted_value);
        Owned<Value> converted(converted_value);
        SmiViewStorage storage;
        ConstBigIntView view =
            intlike_value_bigint_view(converted.value(), &storage);
        return make_heap_bigint(thread, cls, view).raw_value();
    }

    [[nodiscard]] static Value native_int_new(ThreadState *thread,
                                              Value cls_value, Value obj)
    {
        if(cls_value == Value::from_oop(thread->get_machine()->int_class()))
        {
            return convert_to_exact_int(thread, obj);
        }
        return int_new_subclass_slow(thread, cls_value, obj);
    }

    static Value native_int_str(ThreadState *thread, Value self)
    {
        if(self.is_smi())
        {
            return thread
                ->make_object_value<String>(std::to_wstring(self.get_smi()))
                .raw_value();
        }
        if(can_convert_to<BigInt>(self))
        {
            BigInt *bigint = assume_convert_to<BigInt>(self);
            return thread
                ->make_object_value<String>(
                    bigint_to_decimal_string(bigint->view()))
                .raw_value();
        }

        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"int.__str__ expects an int receiver");
    }

    [[nodiscard]] static Value native_int_is_integer(ThreadState *thread,
                                                     Value self)
    {
        if(!is_intlike_value(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"int.is_integer expects an int receiver");
        }
        return Value::True();
    }

    [[nodiscard]] static Value native_int_bit_length(ThreadState *thread,
                                                     Value self)
    {
        if(is_smi_or_bool_value(self))
        {
            int64_t value = smi_or_bool_as_int(self);
            uint64_t magnitude =
                value < 0 ? uint64_t{0} - static_cast<uint64_t>(value)
                          : static_cast<uint64_t>(value);
            return Value::from_smi(std::bit_width(magnitude));
        }
        if(can_convert_to<BigInt>(self))
        {
            return CL_TRY(int_value_from_uint64(
                thread, bigint_abs_bit_length(self.get_ptr<BigInt>()->view())));
        }
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"int.bit_length expects an int receiver");
    }

    [[nodiscard]] static Value native_int_bit_count(ThreadState *thread,
                                                    Value self)
    {
        if(is_smi_or_bool_value(self))
        {
            int64_t value = smi_or_bool_as_int(self);
            uint64_t magnitude =
                value < 0 ? uint64_t{0} - static_cast<uint64_t>(value)
                          : static_cast<uint64_t>(value);
            return Value::from_smi(std::popcount(magnitude));
        }
        if(can_convert_to<BigInt>(self))
        {
            return CL_TRY(int_value_from_uint64(
                thread, bigint_abs_bit_count(self.get_ptr<BigInt>()->view())));
        }
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"int.bit_count expects an int receiver");
    }

    static Value intlike_hash_value(Value self)
    {
        Expected<TValue<SMI>> hash = canonicalize_hash_result(self);
        return hash.value().raw_value();
    }

    static Value native_int_hash(ThreadState *thread, Value self)
    {
        if(!is_intlike_value(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"int.__hash__ expects an int receiver");
        }
        return intlike_hash_value(self);
    }

    static Value intlike_bool_value(Value self)
    {
        bool truth = is_smi_or_bool_value(self)
                         ? smi_or_bool_as_int(self) != 0
                         : self.get_ptr<BigInt>()->signum() != 0;
        return truth ? Value::True() : Value::False();
    }

    static Value native_int_bool(ThreadState *thread, Value self)
    {
        if(!is_intlike_value(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"int.__bool__ expects an int receiver");
        }
        return intlike_bool_value(self);
    }

    static bool try_get_smi_or_bool(Value value, int64_t *out)
    {
        if(unlikely(is_smi_or_bool_value(value)))
        {
            *out = smi_or_bool_as_int(value);
            return true;
        }
        return false;
    }

    static Value int_overflow_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"OverflowError", L"integer overflow");
    }

    static Value int_zero_division_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"ZeroDivisionError", L"division by zero");
    }

    static Value int_negative_shift_count_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"ValueError", L"negative shift count");
    }

    static Value int_result_or_overflow(ThreadState *thread, int64_t result)
    {
        if(unlikely(result < value_smi_min || result > value_smi_max))
        {
            return int_overflow_error(thread);
        }
        return Value::from_smi(result);
    }

    struct SMIAddOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__add__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            int64_t result;
            if(unlikely(__builtin_add_overflow(left, right, &result)))
            {
                return int_overflow_error(thread);
            }
            return int_result_or_overflow(thread, result);
        }
    };

    struct SMIRAddOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__radd__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIAddOperator{}(thread, left, right);
        }
    };

    struct SMISubOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__sub__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            int64_t result;
            if(unlikely(__builtin_sub_overflow(left, right, &result)))
            {
                return int_overflow_error(thread);
            }
            return int_result_or_overflow(thread, result);
        }
    };

    struct SMIRSubOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rsub__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMISubOperator{}(thread, right, left);
        }
    };

    struct SMIMulOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__mul__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            int64_t result;
            if(unlikely(__builtin_mul_overflow(left, right, &result)))
            {
                return int_overflow_error(thread);
            }
            return int_result_or_overflow(thread, result);
        }
    };

    struct SMIRMulOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rmul__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIMulOperator{}(thread, left, right);
        }
    };

    struct IntAddOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__add__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return bigint_add(thread, left, right).raw_value();
        }
    };

    struct IntRAddOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__radd__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntAddOperator{}(thread, left, right);
        }
    };

    struct IntSubOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__sub__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return bigint_sub(thread, left, right).raw_value();
        }
    };

    struct IntRSubOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rsub__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntSubOperator{}(thread, right, left);
        }
    };

    struct IntMulOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__mul__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return bigint_mul(thread, left, right).raw_value();
        }
    };

    struct IntRMulOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rmul__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntMulOperator{}(thread, left, right);
        }
    };

    struct SMITrueDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__truediv__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            if(unlikely(right == 0))
            {
                return int_zero_division_error(thread);
            }
            return thread
                ->make_object_value<Float>(double(left) / double(right))
                .raw_value();
        }
    };

    struct SMIRTrueDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rtruediv__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMITrueDivOperator{}(thread, right, left);
        }
    };

    static Value int_zero_to_negative_power_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"ZeroDivisionError", L"zero to a negative power");
    }

    static Value int_float_pow_result(ThreadState *thread, double base,
                                      double exponent, bool negative_result)
    {
        double magnitude = std::pow(std::abs(base), exponent);
        if(std::isinf(magnitude))
        {
            return thread->set_pending_builtin_exception_string(
                L"OverflowError", L"float power result too large");
        }
        double result = negative_result ? -magnitude : magnitude;
        return thread->make_object_value<Float>(result).raw_value();
    }

    static Value int_bigint_negative_exponent_pow(ThreadState *thread,
                                                  ConstBigIntView base,
                                                  ConstBigIntView exponent)
    {
        assert(exponent.signum < 0);
        Expected<double> exponent_double = bigint_to_double(exponent);
        if(exponent_double.has_exception())
        {
            return Value::exception_marker();
        }
        Expected<double> base_double = bigint_to_double(base);
        if(base_double.has_exception())
        {
            return Value::exception_marker();
        }
        if(base.signum == 0)
        {
            return int_zero_to_negative_power_error(thread);
        }
        bool negative_result = base.signum < 0 && bigint_is_odd(exponent);
        return int_float_pow_result(thread, base_double.value(),
                                    exponent_double.value(), negative_result);
    }

    static Value int_smi_pow_nonnegative(ThreadState *thread, int64_t base,
                                         int64_t exponent)
    {
        assert(exponent >= 0);
        int64_t result = 1;
        int64_t power = base;
        uint64_t remaining = static_cast<uint64_t>(exponent);

        auto fallback_to_bigint = [&]() {
            SmiViewStorage base_storage;
            SmiViewStorage exponent_storage;
            return bigint_pow_nonnegative(
                       thread, smi_bigint_view(base, &base_storage),
                       smi_bigint_view(exponent, &exponent_storage))
                .raw_value();
        };

        while(remaining != 0)
        {
            if((remaining & 1) != 0)
            {
                int64_t product;
                if(unlikely(__builtin_mul_overflow(result, power, &product)))
                {
                    return fallback_to_bigint();
                }
                if(unlikely(product < value_smi_min || product > value_smi_max))
                {
                    return fallback_to_bigint();
                }
                result = product;
            }
            remaining >>= 1;
            if(remaining != 0)
            {
                int64_t square;
                if(unlikely(__builtin_mul_overflow(power, power, &square)))
                {
                    return fallback_to_bigint();
                }
                if(unlikely(square < value_smi_min || square > value_smi_max))
                {
                    return fallback_to_bigint();
                }
                power = square;
            }
        }

        return Value::from_smi(result);
    }

    struct SMIPowOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__pow__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            if(right == 0)
            {
                return Value::from_smi(1);
            }
            if(right < 0)
            {
                if(left == 0)
                {
                    return int_zero_to_negative_power_error(thread);
                }
                bool negative_result = left < 0 && (right & 1) != 0;
                return int_float_pow_result(thread, static_cast<double>(left),
                                            static_cast<double>(right),
                                            negative_result);
            }
            return int_smi_pow_nonnegative(thread, left, right);
        }
    };

    struct SMIRPowOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rpow__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIPowOperator{}(thread, right, left);
        }
    };

    struct IntPowOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__pow__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            if(right.signum == 0)
            {
                return Value::from_smi(1);
            }
            if(right.signum < 0)
            {
                return int_bigint_negative_exponent_pow(thread, left, right);
            }
            return bigint_pow_nonnegative(thread, left, right).raw_value();
        }
    };

    struct IntRPowOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rpow__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntPowOperator{}(thread, right, left);
        }
    };

    static int64_t floor_div_smi_values(int64_t left, int64_t right)
    {
        int64_t quotient = left / right;
        int64_t remainder = left % right;
        if(remainder != 0 && ((remainder < 0) != (right < 0)))
        {
            quotient -= 1;
        }
        return quotient;
    }

    static int64_t modulo_smi_values(int64_t left, int64_t right)
    {
        int64_t remainder = left % right;
        if(remainder != 0 && ((remainder < 0) != (right < 0)))
        {
            remainder += right;
        }
        return remainder;
    }

    enum class ShiftCountStatus
    {
        Converted,
        TooLarge
    };

    static Expected<ShiftCountStatus>
    bigint_view_to_shift_count(ConstBigIntView view, uint64_t *out)
    {
        assert(is_normalized_bigint_view(view));
        if(view.signum < 0)
        {
            return Expected<ShiftCountStatus>::raise_exception(
                L"ValueError", L"negative shift count");
        }
        if(view.n_digits > 2)
        {
            return Expected<ShiftCountStatus>::ok(ShiftCountStatus::TooLarge);
        }

        uint64_t shift = view.n_digits > 0 ? view.digits[0] : 0;
        if(view.n_digits > 1)
        {
            shift |= uint64_t(view.digits[1]) << kDigitBits;
        }
        *out = shift;
        return Expected<ShiftCountStatus>::ok(ShiftCountStatus::Converted);
    }

    struct SMIFloorDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__floordiv__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            if(unlikely(right == 0))
            {
                return int_zero_division_error(thread);
            }
            if(unlikely(left == value_smi_min && right == -1))
            {
                return int_overflow_error(thread);
            }
            return Value::from_smi(floor_div_smi_values(left, right));
        }
    };

    struct SMIRFloorDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rfloordiv__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIFloorDivOperator{}(thread, right, left);
        }
    };

    struct IntFloorDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__floordiv__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return bigint_floor_div(thread, left, right).raw_value();
        }
    };

    struct IntRFloorDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rfloordiv__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntFloorDivOperator{}(thread, right, left);
        }
    };

    struct SMIModOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__mod__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            if(unlikely(right == 0))
            {
                return int_zero_division_error(thread);
            }
            return Value::from_smi(modulo_smi_values(left, right));
        }
    };

    struct IntModOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__mod__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return bigint_mod(thread, left, right).raw_value();
        }
    };

    struct IntRModOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rmod__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntModOperator{}(thread, right, left);
        }
    };

    struct SMIRModOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rmod__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIModOperator{}(thread, right, left);
        }
    };

    struct SMILShiftOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__lshift__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            if(unlikely(right < 0))
            {
                return int_negative_shift_count_error(thread);
            }
            int64_t headroom_bits = count_left_shift_headroom_bits(left);
            if(unlikely(uint64_t(right) >= uint64_t(int64_storage_bits) ||
                        (left != 0 &&
                         right + int64_t(value_tag_bits) > headroom_bits)))
            {
                return int_overflow_error(thread);
            }
            Value left_value = Value::from_smi(left);
            Value result;
            result.as.integer =
                static_cast<int64_t>(uint64_t(left_value.as.integer) << right);
            return result;
        }
    };

    struct SMIRLShiftOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rlshift__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMILShiftOperator{}(thread, right, left);
        }
    };

    struct IntLShiftOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__lshift__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            uint64_t shift_amount = 0;
            Expected<ShiftCountStatus> status =
                bigint_view_to_shift_count(right, &shift_amount);
            if(status.has_exception())
            {
                return Value::exception_marker();
            }
            if(status.value() == ShiftCountStatus::TooLarge)
            {
                return int_overflow_error(thread);
            }
            return bigint_lshift(thread, left, shift_amount).raw_value();
        }
    };

    struct IntRLShiftOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rlshift__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntLShiftOperator{}(thread, right, left);
        }
    };

    struct SMIRShiftOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rshift__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            if(unlikely(right < 0))
            {
                return int_negative_shift_count_error(thread);
            }
            if(unlikely(right >= int64_storage_bits))
            {
                return Value::from_smi(left >> int64_value_bits);
            }
            return Value::from_smi(left >> right);
        }
    };

    struct IntRShiftOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rshift__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            uint64_t shift_amount = 0;
            Expected<ShiftCountStatus> status =
                bigint_view_to_shift_count(right, &shift_amount);
            if(status.has_exception())
            {
                return Value::exception_marker();
            }
            if(status.value() == ShiftCountStatus::TooLarge)
            {
                return Value::from_smi(left.signum < 0 ? -1 : 0);
            }
            return bigint_rshift(thread, left, shift_amount).raw_value();
        }
    };

    struct IntRRShiftOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rrshift__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntRShiftOperator{}(thread, right, left);
        }
    };

    struct SMIRRShiftOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rrshift__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIRShiftOperator{}(thread, right, left);
        }
    };

    struct SMIAndOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__and__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            (void)thread;
            return Value::from_smi(left & right);
        }
    };

    struct SMIRAndOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rand__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIAndOperator{}(thread, right, left);
        }
    };

    struct SMIXorOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__xor__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            (void)thread;
            return Value::from_smi(left ^ right);
        }
    };

    struct SMIRXorOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rxor__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIXorOperator{}(thread, right, left);
        }
    };

    struct SMIOrOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__or__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            (void)thread;
            return Value::from_smi(left | right);
        }
    };

    struct SMIROrOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__ror__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t left, int64_t right) const
        {
            return SMIOrOperator{}(thread, right, left);
        }
    };

    struct IntAndOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__and__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return bigint_and(thread, left, right).raw_value();
        }
    };

    struct IntRAndOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rand__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntAndOperator{}(thread, right, left);
        }
    };

    struct IntXorOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__xor__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return bigint_xor(thread, left, right).raw_value();
        }
    };

    struct IntRXorOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__rxor__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntXorOperator{}(thread, right, left);
        }
    };

    struct IntOrOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__or__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return bigint_or(thread, left, right).raw_value();
        }
    };

    struct IntROrOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__ror__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            return IntOrOperator{}(thread, right, left);
        }
    };

    struct SMINegOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__neg__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t value) const
        {
            if(unlikely(value == value_smi_min))
            {
                SmiViewStorage smi_storage;
                Expected<Value> result =
                    bigint_negate(thread, smi_bigint_view(value, &smi_storage));
                if(result.has_exception())
                {
                    return Value::exception_marker();
                }
                return result.value();
            }
            return Value::from_smi(-value);
        }
    };

    struct SMIAbsOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__abs__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t value) const
        {
            return value < 0 ? SMINegOperator{}(thread, value)
                             : Value::from_smi(value);
        }
    };

    static Value trusted_bigint_abs(ThreadState *thread, Value self)
    {
        BigInt *bigint = self.get_ptr<BigInt>();
        if(bigint->view().signum < 0)
        {
            return CL_TRY(bigint_negate(thread, bigint->view()));
        }
        return self;
    }

    static Value native_int_abs(ThreadState *thread, Value self)
    {
        int64_t value;
        if(try_get_smi_or_bool(self, &value))
        {
            return SMIAbsOperator{}(thread, value);
        }
        if(can_convert_to<BigInt>(self))
        {
            BigInt *integer = self.get_ptr<BigInt>();
            if(integer->signum() < 0 || integer->get_shape()->get_class() ==
                                            thread->get_machine()->int_class())
            {
                return trusted_bigint_abs(thread, self);
            }
            return CL_TRY(finalize_bigint(thread, integer->view()));
        }
        return thread->set_pending_builtin_exception_string(
            L"TypeError", SMIAbsOperator::receiver_error);
    }

    struct SMIPosOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__pos__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t value) const
        {
            (void)thread;
            return Value::from_smi(value);
        }
    };

    struct SMIInvertOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__invert__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t value) const
        {
            (void)thread;
            return Value::from_smi(~value);
        }
    };

    struct IntInvertOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__invert__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView value) const
        {
            return bigint_invert(thread, value).raw_value();
        }
    };

    struct IntEqOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__eq__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            (void)thread;
            return compare_bigint(left, right) == 0 ? Value::True()
                                                    : Value::False();
        }
    };

    struct IntNeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__ne__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            (void)thread;
            return compare_bigint(left, right) != 0 ? Value::True()
                                                    : Value::False();
        }
    };

    struct IntLtOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__lt__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            (void)thread;
            return compare_bigint(left, right) < 0 ? Value::True()
                                                   : Value::False();
        }
    };

    struct IntLeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__le__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            (void)thread;
            return compare_bigint(left, right) <= 0 ? Value::True()
                                                    : Value::False();
        }
    };

    struct IntGtOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__gt__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            (void)thread;
            return compare_bigint(left, right) > 0 ? Value::True()
                                                   : Value::False();
        }
    };

    struct IntGeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__ge__ expects an int receiver";

        Value operator()(ThreadState *thread, ConstBigIntView left,
                         ConstBigIntView right) const
        {
            (void)thread;
            return compare_bigint(left, right) >= 0 ? Value::True()
                                                    : Value::False();
        }
    };

    template <typename Operator>
    static Value native_int_unary_operator(ThreadState *thread, Value self)
    {
        int64_t value;
        if(!try_get_smi_or_bool(self, &value))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }

        return Operator{}(thread, value);
    }

    static Value native_int_neg(ThreadState *thread, Value self)
    {
        int64_t value;
        if(try_get_smi_or_bool(self, &value))
        {
            return SMINegOperator{}(thread, value);
        }
        if(can_convert_to<BigInt>(self))
        {
            BigInt *bigint = assume_convert_to<BigInt>(self);
            Expected<Value> result = bigint_negate(thread, bigint->view());
            if(result.has_exception())
            {
                return Value::exception_marker();
            }
            return result.value();
        }
        return thread->set_pending_builtin_exception_string(
            L"TypeError", SMINegOperator::receiver_error);
    }

    static Value native_int_pos(ThreadState *thread, Value self)
    {
        if(self.is_smi())
        {
            return self;
        }
        if(can_convert_to<BigInt>(self))
        {
            BigInt *integer = self.get_ptr<BigInt>();
            if(integer->get_shape()->get_class() ==
               thread->get_machine()->int_class())
            {
                return self;
            }
            return CL_TRY(finalize_bigint(thread, integer->view()));
        }
        if(self == Value::True())
        {
            return Value::from_smi(1);
        }
        if(self == Value::False())
        {
            return Value::from_smi(0);
        }
        return thread->set_pending_builtin_exception_string(
            L"TypeError", SMIPosOperator::receiver_error);
    }

    template <typename Operator>
    static Value native_int_binary_operator(ThreadState *thread, Value self,
                                            Value other)
    {
        int64_t left;
        if(!try_get_smi_or_bool(self, &left))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }

        int64_t right;
        if(!try_get_smi_or_bool(other, &right))
        {
            return Value::NotImplemented();
        }

        return Operator{}(thread, left, right);
    }

    template <typename Operator>
    static Value native_int_bigint_binary_operator(ThreadState *thread,
                                                   Value self, Value other)
    {
        if(!is_intlike_value(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }
        if(!is_intlike_value(other))
        {
            return Value::NotImplemented();
        }

        SmiViewStorage self_storage;
        SmiViewStorage other_storage;
        ConstBigIntView self_view =
            intlike_value_bigint_view(self, &self_storage);
        ConstBigIntView other_view =
            intlike_value_bigint_view(other, &other_storage);
        return Operator{}(thread, self_view, other_view);
    }

    static Value native_int_binary_pow(ThreadState *thread, Value self,
                                       Value other)
    {
        return native_int_bigint_binary_operator<IntPowOperator>(thread, self,
                                                                 other);
    }

    static Value native_int_binary_rpow(ThreadState *thread, Value self,
                                        Value other)
    {
        return native_int_bigint_binary_operator<IntRPowOperator>(thread, self,
                                                                  other);
    }

    static Value native_int_ternary_pow(ThreadState *thread, Value self,
                                        Value other, Value modulo)
    {
        if(modulo == Value::None())
        {
            return native_int_binary_pow(thread, self, other);
        }
        if(!is_intlike_value(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", IntPowOperator::receiver_error);
        }
        if(!is_intlike_value(other) || !is_intlike_value(modulo))
        {
            return Value::NotImplemented();
        }

        SmiViewStorage self_storage;
        SmiViewStorage other_storage;
        SmiViewStorage modulo_storage;
        Expected<Value> result = bigint_modular_pow(
            thread, intlike_value_bigint_view(self, &self_storage),
            intlike_value_bigint_view(other, &other_storage),
            intlike_value_bigint_view(modulo, &modulo_storage));
        if(result.has_exception())
        {
            return Value::exception_marker();
        }
        return result.value();
    }

    static Value native_int_ternary_rpow(ThreadState *thread, Value self,
                                         Value other, Value modulo)
    {
        if(modulo == Value::None())
        {
            return native_int_binary_rpow(thread, self, other);
        }
        if(!is_intlike_value(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", IntRPowOperator::receiver_error);
        }
        if(!is_intlike_value(other) || !is_intlike_value(modulo))
        {
            return Value::NotImplemented();
        }

        SmiViewStorage self_storage;
        SmiViewStorage other_storage;
        SmiViewStorage modulo_storage;
        Expected<Value> result = bigint_modular_pow(
            thread, intlike_value_bigint_view(other, &other_storage),
            intlike_value_bigint_view(self, &self_storage),
            intlike_value_bigint_view(modulo, &modulo_storage));
        if(result.has_exception())
        {
            return Value::exception_marker();
        }
        return result.value();
    }

    template <typename Operator>
    static Value native_int_bigint_unary_operator(ThreadState *thread,
                                                  Value self)
    {
        if(!is_intlike_value(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }

        SmiViewStorage storage;
        ConstBigIntView view = intlike_value_bigint_view(self, &storage);
        return Operator{}(thread, view);
    }

    template <typename Operator>
    static Value native_int_compare_operator(ThreadState *thread, Value self,
                                             Value other)
    {
        if(!is_intlike_value(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }
        if(!is_intlike_value(other))
        {
            return Value::NotImplemented();
        }

        SmiViewStorage left_storage;
        SmiViewStorage right_storage;
        ConstBigIntView left = intlike_value_bigint_view(self, &left_storage);
        ConstBigIntView right =
            intlike_value_bigint_view(other, &right_storage);
        return Operator{}(thread, left, right);
    }

    template <typename Operator>
    static Value trusted_intlike_intlike_operator(ThreadState *thread,
                                                  Value left_value,
                                                  Value right_value)
    {
        return Operator{}(thread, smi_or_bool_as_int(left_value),
                          smi_or_bool_as_int(right_value));
    }

    template <typename Operator>
    static Value trusted_intlike_bigint_operator(ThreadState *thread,
                                                 Value left_value,
                                                 Value right_value)
    {
        SmiViewStorage left_storage;
        SmiViewStorage right_storage;
        ConstBigIntView left =
            intlike_value_bigint_view(left_value, &left_storage);
        ConstBigIntView right =
            intlike_value_bigint_view(right_value, &right_storage);
        return Operator{}(thread, left, right);
    }

    template <typename Operator>
    static Value trusted_intlike_bigint_unary_operator(ThreadState *thread,
                                                       Value value)
    {
        SmiViewStorage storage;
        ConstBigIntView view = intlike_value_bigint_view(value, &storage);
        return Operator{}(thread, view);
    }

    template <typename Operator>
    static Value trusted_intlike_unary_operator(ThreadState *thread,
                                                Value value)
    {
        return Operator{}(thread, smi_or_bool_as_int(value));
    }

    static Value trusted_smi_or_bool_truth(ThreadState *thread, Value self)
    {
        (void)thread;
        return smi_or_bool_as_int(self) != 0 ? Value::True() : Value::False();
    }

    static Value trusted_bigint_truth(ThreadState *thread, Value self)
    {
        (void)thread;
        return self.get_ptr<BigInt>()->signum() != 0 ? Value::True()
                                                     : Value::False();
    }

    using IntInlineBoolHandler =
        TrustedHandlerDefinition<trusted_smi_or_bool_truth,
                                 TrustedHandlerEffects::None>;
    using IntBigIntBoolHandler =
        TrustedHandlerDefinition<trusted_bigint_truth,
                                 TrustedHandlerEffects::None>;

    static Value trusted_smi_or_bool_hash(ThreadState *thread, Value value)
    {
        (void)thread;
        int64_t hash = smi_or_bool_as_int(value);
        return Value::from_smi(hash == -1 ? -2 : hash);
    }

    static Value trusted_bigint_hash(ThreadState *thread, Value value)
    {
        (void)thread;
        return intlike_hash_value(value);
    }

    static bool is_smi_or_bool_shape_key(ShapeKey key)
    {
        return key == ShapeKey::from_value(Value::from_smi(0)) ||
               key == ShapeKey::from_value(Value::False());
    }

    static bool is_intlike_shape_key(VirtualMachine *vm, ShapeKey key)
    {
        return is_smi_or_bool_shape_key(key) ||
               key == ShapeKey::from_shape(
                          vm->int_class()->get_instance_root_shape());
    }

    static bool is_float_shape_key(VirtualMachine *vm, ShapeKey key)
    {
        return key == ShapeKey::from_shape(
                          vm->float_class()->get_instance_root_shape());
    }

    static bool is_bigint_shape_key(VirtualMachine *vm, ShapeKey key)
    {
        return key ==
               ShapeKey::from_shape(vm->int_class()->get_instance_root_shape());
    }

    using SMIAbsHandler =
        TrustedHandlerDefinition<trusted_intlike_unary_operator<SMIAbsOperator>,
                                 TrustedHandlerEffects::Allocate |
                                     TrustedHandlerEffects::Raise,
                                 TrustedHandlerSemantics::Generic>;
    using BigIntAbsHandler =
        TrustedHandlerDefinition<trusted_bigint_abs,
                                 TrustedHandlerEffects::Allocate |
                                     TrustedHandlerEffects::Raise,
                                 TrustedHandlerSemantics::Generic>;

    static TrustedResolution resolve_trusted_int_abs_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        (void)operand1_key;
        (void)order;
        if(requested_arity != TrustedHandlerArity::Unary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(is_smi_or_bool_shape_key(operand0_key))
        {
            return SMIAbsHandler::resolution();
        }
        if(is_bigint_shape_key(vm, operand0_key))
        {
            return BigIntAbsHandler::resolution();
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    class IntBoolResolver final
        : public TrustedHandlerResolverBase<IntInlineBoolHandler,
                                            IntBigIntBoolHandler>
    {
    public:
        static TrustedResolution resolve(VirtualMachine *vm,
                                         ShapeKey operand0_key,
                                         ShapeKey operand1_key,
                                         TrustedHandlerOperandOrder order,
                                         TrustedHandlerArity requested_arity)
        {
            (void)operand1_key;
            (void)order;
            if(requested_arity != TrustedHandlerArity::Unary)
            {
                return TrustedResolution::no_trusted_handler_call_untrusted();
            }
            if(is_smi_or_bool_shape_key(operand0_key))
            {
                return IntInlineBoolHandler::resolution();
            }
            if(is_bigint_shape_key(vm, operand0_key))
            {
                return IntBigIntBoolHandler::resolution();
            }
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
    };

    static TrustedResolution resolve_trusted_int_hash_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        (void)operand1_key;
        (void)order;

        if(requested_arity != TrustedHandlerArity::Unary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(is_smi_or_bool_shape_key(operand0_key))
        {
            return TrustedResolution::call_trusted(trusted_smi_or_bool_hash);
        }
        if(is_bigint_shape_key(vm, operand0_key))
        {
            return TrustedResolution::call_trusted(trusted_bigint_hash);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename Operator>
    static TrustedResolution resolve_trusted_int_binary_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key)
    {
        if(is_smi_or_bool_shape_key(operand0_key) &&
           is_smi_or_bool_shape_key(operand1_key))
        {
            return TrustedResolution::call_trusted(
                trusted_intlike_intlike_operator<Operator>);
        }
        if((is_smi_or_bool_shape_key(operand0_key) &&
            is_float_shape_key(vm, operand1_key)) ||
           (is_float_shape_key(vm, operand0_key) &&
            is_smi_or_bool_shape_key(operand1_key)))
        {
            return TrustedResolution::known_not_implemented_skip_method();
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename Operator>
    static TrustedResolution resolve_trusted_int_bigint_binary_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key)
    {
        if(is_intlike_shape_key(vm, operand0_key) &&
           is_intlike_shape_key(vm, operand1_key))
        {
            return TrustedResolution::call_trusted(
                trusted_intlike_bigint_operator<Operator>);
        }
        if((is_intlike_shape_key(vm, operand0_key) &&
            is_float_shape_key(vm, operand1_key)) ||
           (is_float_shape_key(vm, operand0_key) &&
            is_intlike_shape_key(vm, operand1_key)))
        {
            return TrustedResolution::known_not_implemented_skip_method();
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename NormalOperator, typename ReflectedOperator>
    static TrustedResolution resolve_trusted_int_binary_resolver(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(order == TrustedHandlerOperandOrder::Reflected)
        {
            return resolve_trusted_int_binary_handler<ReflectedOperator>(
                vm, operand0_key, operand1_key);
        }
        return resolve_trusted_int_binary_handler<NormalOperator>(
            vm, operand0_key, operand1_key);
    }

    template <typename NormalOperator, typename ReflectedOperator>
    static TrustedResolution resolve_trusted_int_bigint_binary_resolver(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(order == TrustedHandlerOperandOrder::Reflected)
        {
            return resolve_trusted_int_bigint_binary_handler<ReflectedOperator>(
                vm, operand0_key, operand1_key);
        }
        return resolve_trusted_int_bigint_binary_handler<NormalOperator>(
            vm, operand0_key, operand1_key);
    }

    template <typename SMIOperator, typename BigIntOperator>
    static TrustedResolution resolve_trusted_int_smi_or_bigint_binary_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key)
    {
        if(is_smi_or_bool_shape_key(operand0_key) &&
           is_smi_or_bool_shape_key(operand1_key))
        {
            return TrustedResolution::call_trusted(
                trusted_intlike_intlike_operator<SMIOperator>);
        }
        if(is_intlike_shape_key(vm, operand0_key) &&
           is_intlike_shape_key(vm, operand1_key))
        {
            return TrustedResolution::call_trusted(
                trusted_intlike_bigint_operator<BigIntOperator>);
        }
        if((is_intlike_shape_key(vm, operand0_key) &&
            is_float_shape_key(vm, operand1_key)) ||
           (is_float_shape_key(vm, operand0_key) &&
            is_intlike_shape_key(vm, operand1_key)))
        {
            return TrustedResolution::known_not_implemented_skip_method();
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename NormalSMIOperator, typename ReflectedSMIOperator,
              typename NormalBigIntOperator, typename ReflectedBigIntOperator>
    static TrustedResolution resolve_trusted_int_smi_or_bigint_binary_resolver(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(order == TrustedHandlerOperandOrder::Reflected)
        {
            return resolve_trusted_int_smi_or_bigint_binary_handler<
                ReflectedSMIOperator, ReflectedBigIntOperator>(vm, operand0_key,
                                                               operand1_key);
        }
        return resolve_trusted_int_smi_or_bigint_binary_handler<
            NormalSMIOperator, NormalBigIntOperator>(vm, operand0_key,
                                                     operand1_key);
    }

    static TrustedResolution resolve_trusted_int_pow_resolver(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        if(requested_arity == TrustedHandlerArity::Binary)
        {
            return resolve_trusted_int_smi_or_bigint_binary_resolver<
                SMIPowOperator, SMIRPowOperator, IntPowOperator,
                IntRPowOperator>(vm, operand0_key, operand1_key, order,
                                 requested_arity);
        }
        if(requested_arity == TrustedHandlerArity::Ternary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_int_rpow_resolver(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        if(requested_arity == TrustedHandlerArity::Binary)
        {
            return resolve_trusted_int_smi_or_bigint_binary_resolver<
                SMIRPowOperator, SMIPowOperator, IntRPowOperator,
                IntPowOperator>(vm, operand0_key, operand1_key, order,
                                requested_arity);
        }
        if(requested_arity == TrustedHandlerArity::Ternary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename SMIOperator, typename BigIntOperator>
    static TrustedResolution resolve_trusted_int_smi_or_bigint_unary_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        (void)operand1_key;
        (void)order;

        if(requested_arity != TrustedHandlerArity::Unary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(is_smi_or_bool_shape_key(operand0_key))
        {
            return TrustedResolution::call_trusted(
                trusted_intlike_unary_operator<SMIOperator>);
        }
        if(is_intlike_shape_key(vm, operand0_key))
        {
            return TrustedResolution::call_trusted(
                trusted_intlike_bigint_unary_operator<BigIntOperator>);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename Operator>
    static TrustedResolution resolve_trusted_int_unary_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        (void)vm;
        (void)operand1_key;
        (void)order;

        if(requested_arity != TrustedHandlerArity::Unary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(is_smi_or_bool_shape_key(operand0_key))
        {
            return TrustedResolution::call_trusted(
                trusted_intlike_unary_operator<Operator>);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    BuiltinClassDefinition make_int_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"int"), 0, nullptr, 0,
            vm->object_class(), NativeLayoutId::BigInt, SubclassPolicy::Allowed,
            immutable_shape_flags());
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::BigInt};
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_int_class_methods(VirtualMachine *vm)
    {
        SMIAbsHandler::register_with(*vm);
        BigIntAbsHandler::register_with(*vm);
        Owned<TValue<Tuple>> int_new_defaults(
            active_thread()->make_object_value<Tuple>(1));
        int_new_defaults.extract()->initialize_item_unchecked(
            0, Value::from_smi(0));
        Owned<TValue<Tuple>> int_pow_defaults(
            active_thread()->make_object_value<Tuple>(1));
        int_pow_defaults.extract()->initialize_item_unchecked(0, Value::None());
        BuiltinIntrinsicMethod methods[] = {
            with_defaults(builtin_intrinsic_method(L"__new__", native_int_new,
                                                   L"Create an int object."),
                          int_new_defaults.value()),
            builtin_intrinsic_method(L"__str__", native_int_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_int_str,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"is_integer", native_int_is_integer,
                                     L"Return True for integers."),
            builtin_intrinsic_method(L"bit_length", native_int_bit_length,
                                     L"Return the bit length of abs(self)."),
            builtin_intrinsic_method(
                L"bit_count", native_int_bit_count,
                L"Return the number of set bits in abs(self)."),
            with_trusted_handler_resolver<IntBoolResolver>(
                vm, builtin_intrinsic_method(L"__bool__", native_int_bool,
                                             L"Return self != 0.")),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__hash__", native_int_hash,
                                         L"Return hash(self)."),
                resolve_trusted_int_hash_handler),
            builtin_intrinsic_method(L"__eq__",
                                     native_int_compare_operator<IntEqOperator>,
                                     L"Return self == value."),
            builtin_intrinsic_method(L"__ne__",
                                     native_int_compare_operator<IntNeOperator>,
                                     L"Return self != value."),
            builtin_intrinsic_method(L"__lt__",
                                     native_int_compare_operator<IntLtOperator>,
                                     L"Return self < value."),
            builtin_intrinsic_method(L"__le__",
                                     native_int_compare_operator<IntLeOperator>,
                                     L"Return self <= value."),
            builtin_intrinsic_method(L"__gt__",
                                     native_int_compare_operator<IntGtOperator>,
                                     L"Return self > value."),
            builtin_intrinsic_method(L"__ge__",
                                     native_int_compare_operator<IntGeOperator>,
                                     L"Return self >= value."),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__add__",
                    native_int_bigint_binary_operator<IntAddOperator>,
                    L"Return self + value."),
                resolve_trusted_int_bigint_binary_resolver<IntAddOperator,
                                                           IntRAddOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__radd__",
                    native_int_bigint_binary_operator<IntRAddOperator>,
                    L"Return value + self."),
                resolve_trusted_int_bigint_binary_resolver<IntRAddOperator,
                                                           IntAddOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__sub__",
                    native_int_bigint_binary_operator<IntSubOperator>,
                    L"Return self - value."),
                resolve_trusted_int_bigint_binary_resolver<IntSubOperator,
                                                           IntRSubOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rsub__",
                    native_int_bigint_binary_operator<IntRSubOperator>,
                    L"Return value - self."),
                resolve_trusted_int_bigint_binary_resolver<IntRSubOperator,
                                                           IntSubOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__mul__",
                    native_int_bigint_binary_operator<IntMulOperator>,
                    L"Return self * value."),
                resolve_trusted_int_bigint_binary_resolver<IntMulOperator,
                                                           IntRMulOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rmul__",
                    native_int_bigint_binary_operator<IntRMulOperator>,
                    L"Return value * self."),
                resolve_trusted_int_bigint_binary_resolver<IntRMulOperator,
                                                           IntMulOperator>),
            with_trusted_handler_resolver(
                with_defaults(
                    builtin_intrinsic_method(L"__pow__", native_int_ternary_pow,
                                             L"Return pow(self, value, mod)."),
                    int_pow_defaults.value()),
                resolve_trusted_int_pow_resolver),
            with_trusted_handler_resolver(
                with_defaults(builtin_intrinsic_method(
                                  L"__rpow__", native_int_ternary_rpow,
                                  L"Return pow(value, self, mod)."),
                              int_pow_defaults.value()),
                resolve_trusted_int_rpow_resolver),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__truediv__",
                    native_int_binary_operator<SMITrueDivOperator>,
                    L"Return self / value."),
                resolve_trusted_int_binary_resolver<SMITrueDivOperator,
                                                    SMIRTrueDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rtruediv__",
                    native_int_binary_operator<SMIRTrueDivOperator>,
                    L"Return value / self."),
                resolve_trusted_int_binary_resolver<SMIRTrueDivOperator,
                                                    SMITrueDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__floordiv__",
                    native_int_bigint_binary_operator<IntFloorDivOperator>,
                    L"Return self // value."),
                resolve_trusted_int_bigint_binary_resolver<
                    IntFloorDivOperator, IntRFloorDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rfloordiv__",
                    native_int_bigint_binary_operator<IntRFloorDivOperator>,
                    L"Return value // self."),
                resolve_trusted_int_bigint_binary_resolver<
                    IntRFloorDivOperator, IntFloorDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__mod__",
                    native_int_bigint_binary_operator<IntModOperator>,
                    L"Return self % value."),
                resolve_trusted_int_bigint_binary_resolver<IntModOperator,
                                                           IntRModOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rmod__",
                    native_int_bigint_binary_operator<IntRModOperator>,
                    L"Return value % self."),
                resolve_trusted_int_bigint_binary_resolver<IntRModOperator,
                                                           IntModOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__lshift__",
                    native_int_bigint_binary_operator<IntLShiftOperator>,
                    L"Return self << value."),
                resolve_trusted_int_bigint_binary_resolver<IntLShiftOperator,
                                                           IntRLShiftOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rlshift__",
                    native_int_bigint_binary_operator<IntRLShiftOperator>,
                    L"Return value << self."),
                resolve_trusted_int_bigint_binary_resolver<IntRLShiftOperator,
                                                           IntLShiftOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rshift__",
                    native_int_bigint_binary_operator<IntRShiftOperator>,
                    L"Return self >> value."),
                resolve_trusted_int_bigint_binary_resolver<IntRShiftOperator,
                                                           IntRRShiftOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rrshift__",
                    native_int_bigint_binary_operator<IntRRShiftOperator>,
                    L"Return value >> self."),
                resolve_trusted_int_bigint_binary_resolver<IntRRShiftOperator,
                                                           IntRShiftOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__and__",
                    native_int_bigint_binary_operator<IntAndOperator>,
                    L"Return self & value."),
                resolve_trusted_int_smi_or_bigint_binary_resolver<
                    SMIAndOperator, SMIRAndOperator, IntAndOperator,
                    IntRAndOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rand__",
                    native_int_bigint_binary_operator<IntRAndOperator>,
                    L"Return value & self."),
                resolve_trusted_int_smi_or_bigint_binary_resolver<
                    SMIRAndOperator, SMIAndOperator, IntRAndOperator,
                    IntAndOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__xor__",
                    native_int_bigint_binary_operator<IntXorOperator>,
                    L"Return self ^ value."),
                resolve_trusted_int_smi_or_bigint_binary_resolver<
                    SMIXorOperator, SMIRXorOperator, IntXorOperator,
                    IntRXorOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rxor__",
                    native_int_bigint_binary_operator<IntRXorOperator>,
                    L"Return value ^ self."),
                resolve_trusted_int_smi_or_bigint_binary_resolver<
                    SMIRXorOperator, SMIXorOperator, IntRXorOperator,
                    IntXorOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__or__", native_int_bigint_binary_operator<IntOrOperator>,
                    L"Return self | value."),
                resolve_trusted_int_smi_or_bigint_binary_resolver<
                    SMIOrOperator, SMIROrOperator, IntOrOperator,
                    IntROrOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__ror__",
                    native_int_bigint_binary_operator<IntROrOperator>,
                    L"Return value | self."),
                resolve_trusted_int_smi_or_bigint_binary_resolver<
                    SMIROrOperator, SMIOrOperator, IntROrOperator,
                    IntOrOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__neg__", native_int_neg,
                                         L"Return -self."),
                resolve_trusted_int_unary_handler<SMINegOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__abs__", native_int_abs,
                                         L"Return abs(self)."),
                resolve_trusted_int_abs_handler),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__pos__", native_int_pos,
                                         L"Return +self."),
                resolve_trusted_int_unary_handler<SMIPosOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__invert__",
                    native_int_bigint_unary_operator<IntInvertOperator>,
                    L"Return ~self."),
                resolve_trusted_int_smi_or_bigint_unary_handler<
                    SMIInvertOperator, IntInvertOperator>),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->int_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
