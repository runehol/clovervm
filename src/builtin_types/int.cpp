#include "builtin_types/int.h"

#include "builtin_types/float.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/owned.h"
#include "object_model/value.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cwctype>
#include <iterator>
#include <string>
#include <string_view>

namespace cl
{
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

    bool is_exact_int_value(Value value)
    {
        return value.is_smi() || can_convert_to<BigInt>(value);
    }

    Expected<IntToSmiStatus> try_intlike_value_to_smi(Value value,
                                                      TValue<SMI> *out)
    {
        if(is_smi_or_bool_value(value))
        {
            *out = TValue<SMI>::from_smi(smi_or_bool_as_int(value));
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
                                              SmiBigInt *smi_storage)
    {
        if(is_smi_or_bool_value(value))
        {
            *smi_storage = SmiBigInt(smi_or_bool_as_int(value));
            return smi_storage->view();
        }
        assert(can_convert_to<BigInt>(value));
        return value.get_ptr<BigInt>()->view();
    }

    ConstBigIntView exact_int_value_bigint_view(Value value,
                                                SmiBigInt *smi_storage)
    {
        if(value.is_smi())
        {
            *smi_storage = SmiBigInt(value.get_smi());
            return smi_storage->view();
        }
        assert(can_convert_to<BigInt>(value));
        return value.get_ptr<BigInt>()->view();
    }

    static Value invalid_int_literal(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"ValueError", L"invalid literal for int()");
    }

    static bool is_ascii_digit(cl_wchar ch) { return ch >= L'0' && ch <= L'9'; }

    static Value parse_int_string_fast(ThreadState *thread,
                                       std::wstring_view text)
    {
        if(text.empty())
        {
            return invalid_int_literal(thread);
        }

        size_t digit_begin = 0;
        bool negative = false;
        if(text.front() == L'+' || text.front() == L'-')
        {
            negative = text.front() == L'-';
            digit_begin = 1;
        }

        static constexpr uint64_t positive_limit =
            static_cast<uint64_t>(value_smi_max);
        static constexpr uint64_t negative_limit = positive_limit + 1;
        uint64_t limit = negative ? negative_limit : positive_limit;
        uint64_t parsed = 0;
        bool saw_digit = false;
        for(size_t idx = digit_begin; idx < text.size(); ++idx)
        {
            cl_wchar ch = text[idx];
            if(ch == L'_')
            {
                return Value::not_present();
            }
            if(!is_ascii_digit(ch))
            {
                return invalid_int_literal(thread);
            }

            uint64_t digit = static_cast<uint64_t>(ch - L'0');
            if(parsed > (limit - digit) / 10)
            {
                return Value::not_present();
            }
            parsed = parsed * 10 + digit;
            saw_digit = true;
        }
        if(!saw_digit)
        {
            return invalid_int_literal(thread);
        }

        int64_t result = static_cast<int64_t>(parsed);
        if(negative)
        {
            result = -result;
        }
        return Value::from_smi(result);
    }

    static Value parse_int_string_slow(ThreadState *thread,
                                       std::wstring_view text)
    {
        if(text.empty())
        {
            return invalid_int_literal(thread);
        }

        size_t digit_begin = 0;
        bool negative = false;
        if(text.front() == L'+' || text.front() == L'-')
        {
            negative = text.front() == L'-';
            digit_begin = 1;
        }

        size_t scratch_capacity = text.size() - digit_begin + 1;
        BigIntScratch scratch0(scratch_capacity);
        BigIntScratch scratch1(scratch_capacity);
        MutableBigIntView initial = scratch0.mutable_view();
        ConstBigIntView current{0, 0, initial.digits};
        bool use_first_scratch = false;
        bool saw_digit = false;
        bool previous_underscore = false;

        for(size_t idx = digit_begin; idx < text.size(); ++idx)
        {
            cl_wchar ch = text[idx];
            if(ch == L'_')
            {
                if(!saw_digit || previous_underscore)
                {
                    return invalid_int_literal(thread);
                }
                previous_underscore = true;
                continue;
            }
            if(!is_ascii_digit(ch))
            {
                return invalid_int_literal(thread);
            }

            MutableBigIntView dest = use_first_scratch
                                         ? scratch0.mutable_view()
                                         : scratch1.mutable_view();
            bigint_abs_mul_add_u32(&dest, current, 10,
                                   static_cast<uint32_t>(ch - L'0'));
            current = dest.view();
            use_first_scratch = !use_first_scratch;
            saw_digit = true;
            previous_underscore = false;
        }
        if(!saw_digit || previous_underscore)
        {
            return invalid_int_literal(thread);
        }

        current = normalize_bigint_view(current);
        if(current.signum != 0)
        {
            current.signum = negative ? -1 : 1;
        }
        Expected<Value> result = finalize_bigint(thread, current);
        if(result.has_exception())
        {
            return Value::exception_marker();
        }
        return result.value();
    }

    Expected<Value> parse_int_string_view(ThreadState *thread,
                                          std::wstring_view text)
    {
        size_t begin = 0;
        size_t end = text.size();
        while(begin < end && std::iswspace(text[begin]))
        {
            ++begin;
        }
        while(begin < end && std::iswspace(text[end - 1]))
        {
            --end;
        }
        std::wstring_view trimmed = text.substr(begin, end - begin);

        Value fast = parse_int_string_fast(thread, trimmed);
        if(!fast.is_not_present())
        {
            if(fast.is_exception_marker())
            {
                return Expected<Value>::propagate_exception();
            }
            return Expected<Value>::ok(fast);
        }
        Value slow = parse_int_string_slow(thread, trimmed);
        if(slow.is_exception_marker())
        {
            return Expected<Value>::propagate_exception();
        }
        return Expected<Value>::ok(slow);
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

    static Value native_int_new(ThreadState *thread, Value cls_value, Value obj)
    {
        if(cls_value != Value::from_oop(active_vm()->int_class()))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"int.__new__ expects int as cls");
        }

        if((obj.as.integer & value_not_smi_or_boolean_mask) == 0)
        {
            Value result;
            result.as.integer = obj.as.integer & value_boolean_to_integer_mask;
            return result;
        }

        if(can_convert_to<String>(obj))
        {
            return parse_int_string(thread,
                                    TValue<String>::from_value_assumed(obj));
        }

        return thread->set_pending_builtin_exception_string(
            L"TypeError",
            L"int conversion is only implemented for int, bool and str");
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
            int64_t sign_bits = __builtin_clrsbll(left);
            if(unlikely(
                   uint64_t(right) >= 64 ||
                   (left != 0 && right + int64_t(value_tag_bits) > sign_bits)))
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
            if(unlikely(right >= 64))
            {
                return Value::from_smi(left < 0 ? -1 : 0);
            }
            return Value::from_smi(left >> right);
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

    struct SMINegOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"int.__neg__ expects an int receiver";

        Value operator()(ThreadState *thread, int64_t value) const
        {
            if(unlikely(value == value_smi_min))
            {
                return int_overflow_error(thread);
            }
            return Value::from_smi(-value);
        }
    };

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

        SmiBigInt left_smi(0);
        SmiBigInt right_smi(0);
        ConstBigIntView left = intlike_value_bigint_view(self, &left_smi);
        ConstBigIntView right = intlike_value_bigint_view(other, &right_smi);
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
    static Value trusted_intlike_unary_operator(ThreadState *thread,
                                                Value value)
    {
        return Operator{}(thread, smi_or_bool_as_int(value));
    }

    static bool is_smi_or_bool_shape_key(ShapeKey key)
    {
        return key == ShapeKey::from_value(Value::from_smi(0)) ||
               key == ShapeKey::from_value(Value::False());
    }

    static bool is_float_shape_key(VirtualMachine *vm, ShapeKey key)
    {
        return key == ShapeKey::from_shape(
                          vm->float_class()->get_instance_root_shape());
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
            vm->object_class(), NativeLayoutId::Invalid);
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::BigInt};
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_int_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> int_new_defaults(
            active_thread()->make_object_value<Tuple>(1));
        int_new_defaults.extract()->initialize_item_unchecked(
            0, Value::from_smi(0));
        BuiltinIntrinsicMethod methods[] = {
            with_defaults(builtin_intrinsic_method(L"__new__", native_int_new,
                                                   L"Create an int object."),
                          int_new_defaults.value()),
            builtin_intrinsic_method(L"__str__", native_int_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_int_str,
                                     L"Return repr(self)."),
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
                    L"__add__", native_int_binary_operator<SMIAddOperator>,
                    L"Return self + value."),
                resolve_trusted_int_binary_resolver<SMIAddOperator,
                                                    SMIRAddOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__radd__", native_int_binary_operator<SMIRAddOperator>,
                    L"Return value + self."),
                resolve_trusted_int_binary_resolver<SMIRAddOperator,
                                                    SMIAddOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__sub__", native_int_binary_operator<SMISubOperator>,
                    L"Return self - value."),
                resolve_trusted_int_binary_resolver<SMISubOperator,
                                                    SMIRSubOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rsub__", native_int_binary_operator<SMIRSubOperator>,
                    L"Return value - self."),
                resolve_trusted_int_binary_resolver<SMIRSubOperator,
                                                    SMISubOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__mul__", native_int_binary_operator<SMIMulOperator>,
                    L"Return self * value."),
                resolve_trusted_int_binary_resolver<SMIMulOperator,
                                                    SMIRMulOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rmul__", native_int_binary_operator<SMIRMulOperator>,
                    L"Return value * self."),
                resolve_trusted_int_binary_resolver<SMIRMulOperator,
                                                    SMIMulOperator>),
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
                    native_int_binary_operator<SMIFloorDivOperator>,
                    L"Return self // value."),
                resolve_trusted_int_binary_resolver<SMIFloorDivOperator,
                                                    SMIRFloorDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rfloordiv__",
                    native_int_binary_operator<SMIRFloorDivOperator>,
                    L"Return value // self."),
                resolve_trusted_int_binary_resolver<SMIRFloorDivOperator,
                                                    SMIFloorDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__mod__", native_int_binary_operator<SMIModOperator>,
                    L"Return self % value."),
                resolve_trusted_int_binary_resolver<SMIModOperator,
                                                    SMIRModOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rmod__", native_int_binary_operator<SMIRModOperator>,
                    L"Return value % self."),
                resolve_trusted_int_binary_resolver<SMIRModOperator,
                                                    SMIModOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__lshift__",
                    native_int_binary_operator<SMILShiftOperator>,
                    L"Return self << value."),
                resolve_trusted_int_binary_resolver<SMILShiftOperator,
                                                    SMIRLShiftOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rlshift__",
                    native_int_binary_operator<SMIRLShiftOperator>,
                    L"Return value << self."),
                resolve_trusted_int_binary_resolver<SMIRLShiftOperator,
                                                    SMILShiftOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rshift__",
                    native_int_binary_operator<SMIRShiftOperator>,
                    L"Return self >> value."),
                resolve_trusted_int_binary_resolver<SMIRShiftOperator,
                                                    SMIRRShiftOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rrshift__",
                    native_int_binary_operator<SMIRRShiftOperator>,
                    L"Return value >> self."),
                resolve_trusted_int_binary_resolver<SMIRRShiftOperator,
                                                    SMIRShiftOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__and__", native_int_binary_operator<SMIAndOperator>,
                    L"Return self & value."),
                resolve_trusted_int_binary_resolver<SMIAndOperator,
                                                    SMIRAndOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rand__", native_int_binary_operator<SMIRAndOperator>,
                    L"Return value & self."),
                resolve_trusted_int_binary_resolver<SMIRAndOperator,
                                                    SMIAndOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__xor__", native_int_binary_operator<SMIXorOperator>,
                    L"Return self ^ value."),
                resolve_trusted_int_binary_resolver<SMIXorOperator,
                                                    SMIRXorOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rxor__", native_int_binary_operator<SMIRXorOperator>,
                    L"Return value ^ self."),
                resolve_trusted_int_binary_resolver<SMIRXorOperator,
                                                    SMIXorOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__or__", native_int_binary_operator<SMIOrOperator>,
                    L"Return self | value."),
                resolve_trusted_int_binary_resolver<SMIOrOperator,
                                                    SMIROrOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__ror__", native_int_binary_operator<SMIROrOperator>,
                    L"Return value | self."),
                resolve_trusted_int_binary_resolver<SMIROrOperator,
                                                    SMIOrOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__neg__", native_int_unary_operator<SMINegOperator>,
                    L"Return -self."),
                resolve_trusted_int_unary_handler<SMINegOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__pos__", native_int_unary_operator<SMIPosOperator>,
                    L"Return +self."),
                resolve_trusted_int_unary_handler<SMIPosOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__invert__", native_int_unary_operator<SMIInvertOperator>,
                    L"Return ~self."),
                resolve_trusted_int_unary_handler<SMIInvertOperator>),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->int_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
