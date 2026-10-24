#include "builtin_types/float.h"

#include "builtin_types/bytes.h"
#include "builtin_types/hash.h"
#include "builtin_types/int.h"
#include "builtin_types/num_parse.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/shape_key.h"
#include "runtime/ordering_op.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include "util/fixed_wide_string.h"
#include <array>
#include <cmath>
#include <compare>
#include <cstdint>
#include <fmt/format.h>
#include <iterator>
#include <limits>
#include <string>

namespace cl
{
    Value box_float(ThreadState *thread, double value)
    {
        return thread->make_object_value<Float>(value).raw_value();
    }

    static std::wstring format_float_value(double value)
    {
        if(std::isnan(value))
        {
            return L"nan";
        }
        if(std::isinf(value))
        {
            return std::signbit(value) ? L"-inf" : L"inf";
        }

        std::string text = fmt::format("{}", value);
        if(text.find_first_of(".eE") == std::string::npos)
        {
            text += ".0";
        }
        return std::wstring(text.begin(), text.end());
    }

    static Value native_float_str(ThreadState *thread, Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"float.__str__ expects a float receiver");
        }
        return active_thread()
            ->make_object_value<String>(
                format_float_value(self.get_ptr<Float>()->value()))
            .raw_value();
    }

    static Value native_float_repr(ThreadState *thread, Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"float.__repr__ expects a float receiver");
        }
        return active_thread()
            ->make_object_value<String>(
                format_float_value(self.get_ptr<Float>()->value()))
            .raw_value();
    }

    static bool try_get_float_or_smi_or_bool(Value value, double *out)
    {
        if(unlikely((value.as.integer & value_not_smi_or_boolean_mask) == 0))
        {
            Value integer_value;
            integer_value.as.integer =
                value.as.integer & value_boolean_to_integer_mask;
            *out = static_cast<double>(integer_value.get_smi());
            return true;
        }
        if(has_native_layout<Float>(value))
        {
            *out = value.get_ptr<Float>()->value();
            return true;
        }
        return false;
    }

    static double smi_or_bool_as_double(Value value)
    {
        assert((value.as.integer & value_not_smi_or_boolean_mask) == 0);
        Value integer_value;
        integer_value.as.integer =
            value.as.integer & value_boolean_to_integer_mask;
        return static_cast<double>(integer_value.get_smi());
    }

    [[nodiscard]] static Expected<double>
    float_constructor_value(ThreadState *thread, Value input)
    {
        double value;
        if(try_get_float_or_smi_or_bool(input, &value))
        {
            return Expected<double>::ok(value);
        }
        if(has_native_layout<BigInt>(input))
        {
            return bigint_to_double(input.get_ptr<BigInt>()->view());
        }
        std::optional<double> parsed;
        if(has_native_layout<String>(input))
        {
            parsed = try_parse_float(
                string_view(TValue<String>::from_value_assumed(input)));
        }
        else if(has_native_layout<Bytes>(input))
        {
            auto bytes = bytes_view(TValue<Bytes>::from_value_assumed(input));
            parsed = try_parse_float(std::string_view(
                reinterpret_cast<const char *>(bytes.data()), bytes.size()));
        }
        else
        {
            return Expected<double>::raise_exception(
                L"TypeError",
                L"float conversion is only implemented for float, "
                L"int, bool, str and bytes");
        }
        if(!parsed)
        {
            return Expected<double>::raise_exception(
                L"ValueError", L"could not convert string to float");
        }
        return Expected<double>::ok(*parsed);
    }

    [[nodiscard]] NOINLINE static Value
    float_new_subclass_slow(ThreadState *thread, Value cls_value, Value input)
    {
        if(!has_native_layout<ClassObject>(cls_value))
        {
            TValue<String> name = thread->class_of_value(cls_value)->get_name();
            std::wstring message =
                L"float.__new__(X): X is not a type object (";
            message.append(name.extract()->data,
                           size_t(name.extract()->count.extract()));
            message += L")";
            return thread->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        TValue<ClassObject> cls =
            TValue<ClassObject>::from_value_assumed(cls_value);
        if(!is_subclass_of(cls.extract(), thread->get_machine()->float_class()))
        {
            TValue<String> name = cls.extract()->get_name();
            std::wstring class_name(name.extract()->data,
                                    size_t(name.extract()->count.extract()));
            std::wstring message = L"float.__new__(" + class_name + L"): " +
                                   class_name + L" is not a subtype of float";
            return thread->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        assert(cls.extract()->instance_native_layout_id() ==
               NativeLayoutId::Float);
        assert(cls.extract()->get_instance_inline_slot_capacity() == 0);
        double value = CL_TRY(float_constructor_value(thread, input));
        return thread->make_internal_value<Float>(cls.extract(), value)
            .raw_value();
    }

    [[nodiscard]] static Value native_float_new(ThreadState *thread,
                                                Value cls_value, Value input)
    {
        if(cls_value != Value::from_oop(thread->get_machine()->float_class()))
        {
            return float_new_subclass_slow(thread, cls_value, input);
        }
        if(has_native_layout<Float>(input) &&
           input.get_ptr<Float>()->get_shape()->get_class() ==
               thread->get_machine()->float_class())
        {
            return input;
        }
        double value = CL_TRY(float_constructor_value(thread, input));
        return box_float(thread, value);
    }

    [[nodiscard]] static Value native_float_float(ThreadState *thread,
                                                  Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.__float__ expects a float receiver");
        }
        Float *number = self.get_ptr<Float>();
        if(number->get_shape()->get_class() ==
           thread->get_machine()->float_class())
        {
            return self;
        }
        return box_float(thread, number->value());
    }

    [[nodiscard]] static Value native_float_int(ThreadState *thread, Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.__int__ expects a float receiver");
        }
        return CL_TRY(
            int_value_from_double(thread, self.get_ptr<Float>()->value()));
    }

    [[nodiscard]] static Value native_float_trunc(ThreadState *thread,
                                                  Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.__trunc__ expects a float receiver");
        }
        return CL_TRY(
            int_value_from_double(thread, self.get_ptr<Float>()->value()));
    }

    [[nodiscard]] static Value native_float_floor(ThreadState *thread,
                                                  Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.__floor__ expects a float receiver");
        }
        return CL_TRY(int_value_from_double(
            thread, std::floor(self.get_ptr<Float>()->value())));
    }

    [[nodiscard]] static Value native_float_ceil(ThreadState *thread,
                                                 Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.__ceil__ expects a float receiver");
        }
        return CL_TRY(int_value_from_double(
            thread, std::ceil(self.get_ptr<Float>()->value())));
    }

    [[nodiscard]] static Value native_float_conjugate(ThreadState *thread,
                                                      Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.conjugate expects a float receiver");
        }
        Float *number = self.get_ptr<Float>();
        if(number->get_shape()->get_class() ==
           thread->get_machine()->float_class())
        {
            return self;
        }
        return box_float(thread, number->value());
    }

    [[nodiscard]] static Value native_float_is_integer(ThreadState *thread,
                                                       Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.is_integer expects a float receiver");
        }
        double value = self.get_ptr<Float>()->value();
        return std::isfinite(value) && std::trunc(value) == value
                   ? Value::True()
                   : Value::False();
    }

    // Both payloads are validated by the caller. No conversion of the integer
    // to double is allowed: that can discard the bits that decide equality.
    static bool float_equals_intlike(double number, Value integer)
    {
        assert(is_intlike_value(integer));
        if(!std::isfinite(number) || std::trunc(number) != number)
        {
            return false;
        }
        SmiViewStorage integer_storage;
        ConstBigIntView integer_view =
            intlike_value_bigint_view(integer, &integer_storage);
        signum_t sign = number == 0.0 ? 0 : (number < 0.0 ? -1 : 1);
        if(sign != integer_view.signum)
        {
            return false;
        }
        if(sign == 0)
        {
            return true;
        }

        static_assert(std::numeric_limits<double>::radix == 2);
        static_assert(std::numeric_limits<double>::digits == 53);
        static_assert(std::numeric_limits<double>::max_exponent == 1024);
        int exponent;
        double fraction = std::frexp(std::fabs(number), &exponent);
        if(bigint_abs_bit_length(integer_view) != size_t(exponent))
        {
            return false;
        }
        if(exponent <= 53)
        {
            SmiViewStorage number_storage;
            return compare_bigint_abs(
                       integer_view,
                       smi_bigint_view(static_cast<int64_t>(std::fabs(number)),
                                       &number_storage)) == 0;
        }

        // An integral binary64 needs at most 1024 bits. Expand its 53-bit
        // significand into stack-backed base-2**32 digits, including the zero
        // low digits implied by the exponent. Borrowed views cannot escape.
        std::array<digit_t, 1024 / kDigitBits> digits{};
        uint64_t significand = static_cast<uint64_t>(std::ldexp(fraction, 53));
        unsigned shift = unsigned(exponent - 53);
        size_t index = shift / kDigitBits;
        unsigned offset = shift % kDigitBits;
        digits[index++] = static_cast<digit_t>(significand << offset);
        significand >>= kDigitBits - offset;
        while(significand != 0)
        {
            digits[index++] = static_cast<digit_t>(significand);
            significand >>= kDigitBits;
        }
        return compare_bigint_abs(
                   integer_view, ConstBigIntView{index, 1, digits.data()}) == 0;
    }

    // Caller validates the payload. Views borrow stack or integer storage and
    // never escape; comparing cannot allocate, raise, or invoke Python methods.
    static std::partial_ordering compare_float_intlike(double number,
                                                       Value integer)
    {
        assert(is_intlike_value(integer));
        if(std::isnan(number))
            return std::partial_ordering::unordered;
        if(std::isinf(number))
            return number < 0 ? std::partial_ordering::less
                              : std::partial_ordering::greater;

        SmiViewStorage integer_storage;
        ConstBigIntView view =
            intlike_value_bigint_view(integer, &integer_storage);
        signum_t sign = number == 0.0 ? 0 : (number < 0.0 ? -1 : 1);
        if(sign != view.signum)
            return sign <=> view.signum;
        if(sign == 0)
            return std::partial_ordering::equivalent;

        static_assert(std::numeric_limits<double>::radix == 2);
        static_assert(std::numeric_limits<double>::digits == 53);
        static_assert(std::numeric_limits<double>::max_exponent == 1024);
        double magnitude = std::fabs(number);
        int exponent;
        double fraction = std::frexp(magnitude, &exponent);
        size_t integer_bits = bigint_abs_bit_length(view);
        int comparison;
        if(exponent <= 0 || size_t(exponent) < integer_bits)
            comparison = -1;
        else if(size_t(exponent) > integer_bits)
            comparison = 1;
        else if(exponent <= 53)
        {
            // The truncated magnitude fits exactly; a fractional remainder
            // breaks a tie with the integer toward the larger magnitude.
            int64_t truncated = static_cast<int64_t>(magnitude);
            SmiViewStorage number_storage;
            comparison = compare_bigint_abs(
                smi_bigint_view(truncated, &number_storage), view);
            if(comparison == 0 && magnitude != double(truncated))
                comparison = 1;
        }
        else
        {
            // Binary64 has no fractional bits here. Expand at most 1024 bits,
            // including low zero digits, without converting the integer.
            std::array<digit_t, 1024 / kDigitBits> digits{};
            uint64_t significand =
                static_cast<uint64_t>(std::ldexp(fraction, 53));
            unsigned shift = unsigned(exponent - 53);
            size_t index = shift / kDigitBits;
            unsigned offset = shift % kDigitBits;
            digits[index++] = static_cast<digit_t>(significand << offset);
            significand >>= kDigitBits - offset;
            while(significand != 0)
            {
                digits[index++] = static_cast<digit_t>(significand);
                significand >>= kDigitBits;
            }
            comparison = compare_bigint_abs(
                ConstBigIntView{index, 1, digits.data()}, view);
        }
        if(sign < 0)
            comparison = -comparison;
        return comparison <=> 0;
    }

    static Value float_add(ThreadState *thread, double left, double right)
    {
        return thread->make_object_value<Float>(left + right).raw_value();
    }

    static Value float_subtract(ThreadState *thread, double left, double right)
    {
        return thread->make_object_value<Float>(left - right).raw_value();
    }

    static Value float_reverse_subtract(ThreadState *thread, double left,
                                        double right)
    {
        return thread->make_object_value<Float>(right - left).raw_value();
    }

    static Value float_multiply(ThreadState *thread, double left, double right)
    {
        return thread->make_object_value<Float>(left * right).raw_value();
    }

    static Value float_zero_division_error(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"ZeroDivisionError", L"division by zero");
    }

    struct FloatDivModResult
    {
        double quotient;
        double remainder;
    };

    static FloatDivModResult float_divmod(double left, double right)
    {
        double remainder = std::fmod(left, right);
        double quotient = (left - remainder) / right;

        if(remainder != 0.0)
        {
            if((right < 0.0) != (remainder < 0.0))
            {
                remainder += right;
                quotient -= 1.0;
            }
        }
        else
        {
            remainder = std::copysign(0.0, right);
        }

        double floored_quotient;
        if(quotient != 0.0)
        {
            floored_quotient = std::floor(quotient);
            if(quotient - floored_quotient > 0.5)
            {
                floored_quotient += 1.0;
            }
        }
        else
        {
            floored_quotient = std::copysign(0.0, left / right);
        }
        return {floored_quotient, remainder};
    }

    static Value float_true_divide(ThreadState *thread, double left,
                                   double right)
    {
        if(unlikely(right == 0.0))
        {
            return float_zero_division_error(thread);
        }
        return thread->make_object_value<Float>(left / right).raw_value();
    }

    static Value float_reverse_true_divide(ThreadState *thread, double left,
                                           double right)
    {
        if(unlikely(left == 0.0))
        {
            return float_zero_division_error(thread);
        }
        return thread->make_object_value<Float>(right / left).raw_value();
    }

    static Value float_floor_divide(ThreadState *thread, double left,
                                    double right)
    {
        if(unlikely(right == 0.0))
        {
            return float_zero_division_error(thread);
        }
        return thread
            ->make_object_value<Float>(float_divmod(left, right).quotient)
            .raw_value();
    }

    static Value float_reverse_floor_divide(ThreadState *thread, double left,
                                            double right)
    {
        if(unlikely(left == 0.0))
        {
            return float_zero_division_error(thread);
        }
        return thread
            ->make_object_value<Float>(float_divmod(right, left).quotient)
            .raw_value();
    }

    static Value float_modulo_result(ThreadState *thread, double left,
                                     double right)
    {
        if(unlikely(right == 0.0))
        {
            return float_zero_division_error(thread);
        }

        return thread
            ->make_object_value<Float>(float_divmod(left, right).remainder)
            .raw_value();
    }

    static Value float_modulo(ThreadState *thread, double left, double right)
    {
        return float_modulo_result(thread, left, right);
    }

    static Value float_reverse_modulo(ThreadState *thread, double left,
                                      double right)
    {
        return float_modulo_result(thread, right, left);
    }

    static Value float_negate(ThreadState *thread, double value)
    {
        return thread->make_object_value<Float>(-value).raw_value();
    }

    static Value float_abs(ThreadState *thread, double value)
    {
        return thread->make_object_value<Float>(std::fabs(value)).raw_value();
    }

    using FloatBinaryFunction = Value (*)(ThreadState *, double, double);

    template <typename Operation, TrustedHandlerEffects Effects,
              TrustedHandlerSemantics Semantics>
    struct UniformFloatBinaryHandlers;

    template <FloatBinaryFunction Function, FixedWideString ReceiverError>
    struct FloatBinaryOperation
    {
        using Self = FloatBinaryOperation<Function, ReceiverError>;

        static constexpr auto function = Function;

        static Value native(ThreadState *thread, Value self, Value other)
        {
            if(!has_native_layout<Float>(self))
            {
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", ReceiverError.c_str());
            }

            double right;
            if(!try_get_float_or_smi_or_bool(other, &right))
            {
                return Value::NotImplemented();
            }
            return Function(thread, self.get_ptr<Float>()->value(), right);
        }

        template <TrustedHandlerEffects Effects,
                  TrustedHandlerSemantics Semantics>
        using Handlers = UniformFloatBinaryHandlers<Self, Effects, Semantics>;
    };

    enum class FloatBinaryAdaptation
    {
        FloatFloat,
        FloatIntlike,
        IntlikeFloat,
    };

    template <FloatBinaryFunction Function, FloatBinaryAdaptation Adaptation>
    static Value trusted_adapted_float_binary_operation(ThreadState *thread,
                                                        Value left, Value right)
    {
        if constexpr(Adaptation == FloatBinaryAdaptation::FloatFloat)
        {
            return Function(thread, left.get_ptr<Float>()->value(),
                            right.get_ptr<Float>()->value());
        }
        else if constexpr(Adaptation == FloatBinaryAdaptation::FloatIntlike)
        {
            return Function(thread, left.get_ptr<Float>()->value(),
                            smi_or_bool_as_double(right));
        }
        else
        {
            static_assert(Adaptation == FloatBinaryAdaptation::IntlikeFloat);
            return Function(thread, smi_or_bool_as_double(left),
                            right.get_ptr<Float>()->value());
        }
    }

    template <typename Operation, FloatBinaryAdaptation Adaptation,
              TrustedHandlerEffects Effects, TrustedHandlerSemantics Semantics>
    struct FloatBinaryHandler
        : TrustedHandlerDefinition<trusted_adapted_float_binary_operation<
                                       Operation::function, Adaptation>,
                                   Effects, Semantics>
    {
    };

    template <typename Operation, TrustedHandlerEffects Effects,
              TrustedHandlerSemantics Semantics>
    struct UniformFloatBinaryHandlers
    {
        using FloatFloat =
            FloatBinaryHandler<Operation, FloatBinaryAdaptation::FloatFloat,
                               Effects, Semantics>;
        using FloatIntlike =
            FloatBinaryHandler<Operation, FloatBinaryAdaptation::FloatIntlike,
                               Effects, Semantics>;
        using IntlikeFloat =
            FloatBinaryHandler<Operation, FloatBinaryAdaptation::IntlikeFloat,
                               Effects, Semantics>;
    };

    using FloatUnaryFunction = Value (*)(ThreadState *, double);

    template <typename Operation, TrustedHandlerEffects Effects,
              TrustedHandlerSemantics Semantics>
    struct FloatUnaryHandler;

    template <FloatUnaryFunction Function, FixedWideString ReceiverError>
    struct FloatUnaryOperation
    {
        using Self = FloatUnaryOperation<Function, ReceiverError>;

        static constexpr auto function = Function;

        static Value native(ThreadState *thread, Value self)
        {
            if(!has_native_layout<Float>(self))
            {
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", ReceiverError.c_str());
            }
            return Function(thread, self.get_ptr<Float>()->value());
        }

        template <TrustedHandlerEffects Effects,
                  TrustedHandlerSemantics Semantics>
        using Handler = FloatUnaryHandler<Self, Effects, Semantics>;
    };

    template <FloatUnaryFunction Function>
    static Value trusted_adapted_float_unary_operation(ThreadState *thread,
                                                       Value value)
    {
        return Function(thread, value.get_ptr<Float>()->value());
    }

    template <typename Operation, TrustedHandlerEffects Effects,
              TrustedHandlerSemantics Semantics>
    struct FloatUnaryHandler
        : TrustedHandlerDefinition<
              trusted_adapted_float_unary_operation<Operation::function>,
              Effects, Semantics>
    {
    };

    static bool is_smi_or_bool_shape_key(ShapeKey key)
    {
        return key == ShapeKey::from_value(Value::from_smi(0)) ||
               key == ShapeKey::from_value(Value::False());
    }

    template <typename NormalHandlers, typename ReflectedHandlers,
              bool AcceptHeapInt = false>
    class FloatBinaryResolver final
        : public TrustedHandlerResolverBase<
              typename NormalHandlers::FloatFloat,
              typename NormalHandlers::FloatIntlike,
              typename NormalHandlers::IntlikeFloat,
              typename ReflectedHandlers::FloatFloat,
              typename ReflectedHandlers::FloatIntlike,
              typename ReflectedHandlers::IntlikeFloat>
    {
        template <typename Handlers>
        static TrustedResolution resolve_handlers(VirtualMachine *vm,
                                                  ShapeKey operand0_key,
                                                  ShapeKey operand1_key)
        {
            using FloatFloat = typename Handlers::FloatFloat;
            using FloatIntlike = typename Handlers::FloatIntlike;
            using IntlikeFloat = typename Handlers::IntlikeFloat;

            ShapeKey float_key = ShapeKey::from_shape(
                vm->float_class()->get_instance_root_shape());
            auto is_intlike_key = [vm](ShapeKey key) {
                return is_smi_or_bool_shape_key(key) ||
                       (AcceptHeapInt &&
                        key == ShapeKey::from_shape(
                                   vm->int_class()->get_instance_root_shape()));
            };
            if(operand0_key == float_key)
            {
                if(operand1_key == float_key)
                {
                    return FloatFloat::resolution();
                }
                if(is_intlike_key(operand1_key))
                {
                    return FloatIntlike::resolution();
                }
            }
            if(is_intlike_key(operand0_key) && operand1_key == float_key)
            {
                return IntlikeFloat::resolution();
            }
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }

    public:
        static TrustedResolution resolve(VirtualMachine *vm,
                                         ShapeKey operand0_key,
                                         ShapeKey operand1_key,
                                         TrustedHandlerOperandOrder order,
                                         TrustedHandlerArity requested_arity)
        {
            if(requested_arity != TrustedHandlerArity::Binary)
            {
                return TrustedResolution::no_trusted_handler_call_untrusted();
            }
            if(order == TrustedHandlerOperandOrder::Reflected)
            {
                return resolve_handlers<ReflectedHandlers>(vm, operand0_key,
                                                           operand1_key);
            }
            return resolve_handlers<NormalHandlers>(vm, operand0_key,
                                                    operand1_key);
        }
    };

    template <bool NotEqual, FloatBinaryAdaptation Adaptation>
    static Value trusted_float_equality(ThreadState *thread, Value left,
                                        Value right)
    {
        (void)thread;
        bool equal;
        if constexpr(Adaptation == FloatBinaryAdaptation::FloatFloat)
        {
            equal = left.get_ptr<Float>()->value() ==
                    right.get_ptr<Float>()->value();
        }
        else if constexpr(Adaptation == FloatBinaryAdaptation::FloatIntlike)
        {
            equal = float_equals_intlike(left.get_ptr<Float>()->value(), right);
        }
        else
        {
            equal = float_equals_intlike(right.get_ptr<Float>()->value(), left);
        }
        return equal != NotEqual ? Value::True() : Value::False();
    }

    template <bool NotEqual, FixedWideString ReceiverError>
    struct FloatEqualityOperation
    {
        [[nodiscard]] static Value native(ThreadState *thread, Value self,
                                          Value other)
        {
            if(!has_native_layout<Float>(self))
            {
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", ReceiverError.c_str());
            }
            double number = self.get_ptr<Float>()->value();
            bool equal;
            if(has_native_layout<Float>(other))
            {
                equal = number == other.get_ptr<Float>()->value();
            }
            else if(is_intlike_value(other))
            {
                // Use the actual payload for int subclasses too. Unlike
                // CPython 3.14.5's large-int path, this never re-enters their
                // comparison methods based on the numeric magnitude.
                equal = float_equals_intlike(number, other);
            }
            else
            {
                return Value::NotImplemented();
            }
            return equal != NotEqual ? Value::True() : Value::False();
        }

        template <TrustedHandlerEffects Effects,
                  TrustedHandlerSemantics Semantics>
        struct Handlers
        {
            using FloatFloat = TrustedHandlerDefinition<
                trusted_float_equality<NotEqual,
                                       FloatBinaryAdaptation::FloatFloat>,
                Effects, Semantics>;
            using FloatIntlike = TrustedHandlerDefinition<
                trusted_float_equality<NotEqual,
                                       FloatBinaryAdaptation::FloatIntlike>,
                Effects, Semantics>;
            using IntlikeFloat = TrustedHandlerDefinition<
                trusted_float_equality<NotEqual,
                                       FloatBinaryAdaptation::IntlikeFloat>,
                Effects, Semantics>;
        };
    };

    using FloatEqOperation =
        FloatEqualityOperation<false, L"float.__eq__ expects a float receiver">;
    using FloatEqHandlers =
        FloatEqOperation::Handlers<TrustedHandlerEffects::None,
                                   TrustedHandlerSemantics::Equal>;
    using FloatEqResolver =
        FloatBinaryResolver<FloatEqHandlers, FloatEqHandlers, true>;

    using FloatNeOperation =
        FloatEqualityOperation<true, L"float.__ne__ expects a float receiver">;
    using FloatNeHandlers =
        FloatNeOperation::Handlers<TrustedHandlerEffects::None,
                                   TrustedHandlerSemantics::NotEqual>;
    using FloatNeResolver =
        FloatBinaryResolver<FloatNeHandlers, FloatNeHandlers, true>;

    using FloatAddOperation =
        FloatBinaryOperation<float_add,
                             L"float.__add__ expects a float receiver">;
    using FloatRAddOperation =
        FloatBinaryOperation<float_add,
                             L"float.__radd__ expects a float receiver">;
    using FloatAddHandlers =
        FloatAddOperation::Handlers<TrustedHandlerEffects::Allocate,
                                    TrustedHandlerSemantics::Add>;
    using FloatRAddHandlers =
        FloatRAddOperation::Handlers<TrustedHandlerEffects::Allocate,
                                     TrustedHandlerSemantics::Add>;
    using FloatAddResolver =
        FloatBinaryResolver<FloatAddHandlers, FloatAddHandlers>;
    using FloatRAddResolver =
        FloatBinaryResolver<FloatRAddHandlers, FloatAddHandlers>;

    using FloatSubOperation =
        FloatBinaryOperation<float_subtract,
                             L"float.__sub__ expects a float receiver">;
    using FloatRSubOperation =
        FloatBinaryOperation<float_reverse_subtract,
                             L"float.__rsub__ expects a float receiver">;
    using FloatSubHandlers =
        FloatSubOperation::Handlers<TrustedHandlerEffects::Allocate,
                                    TrustedHandlerSemantics::Sub>;
    using FloatRSubHandlers =
        FloatRSubOperation::Handlers<TrustedHandlerEffects::Allocate,
                                     TrustedHandlerSemantics::RSub>;
    using FloatSubResolver =
        FloatBinaryResolver<FloatSubHandlers, FloatRSubHandlers>;
    using FloatRSubResolver =
        FloatBinaryResolver<FloatRSubHandlers, FloatSubHandlers>;

    using FloatMulOperation =
        FloatBinaryOperation<float_multiply,
                             L"float.__mul__ expects a float receiver">;
    using FloatRMulOperation =
        FloatBinaryOperation<float_multiply,
                             L"float.__rmul__ expects a float receiver">;
    using FloatMulHandlers =
        FloatMulOperation::Handlers<TrustedHandlerEffects::Allocate,
                                    TrustedHandlerSemantics::Mul>;
    using FloatRMulHandlers =
        FloatRMulOperation::Handlers<TrustedHandlerEffects::Allocate,
                                     TrustedHandlerSemantics::Mul>;
    using FloatMulResolver =
        FloatBinaryResolver<FloatMulHandlers, FloatMulHandlers>;
    using FloatRMulResolver =
        FloatBinaryResolver<FloatRMulHandlers, FloatMulHandlers>;

    static constexpr TrustedHandlerEffects FloatRaisingBinaryEffects =
        TrustedHandlerEffects::Allocate | TrustedHandlerEffects::Raise;

    using FloatTrueDivOperation =
        FloatBinaryOperation<float_true_divide,
                             L"float.__truediv__ expects a float receiver">;
    using FloatRTrueDivOperation =
        FloatBinaryOperation<float_reverse_true_divide,
                             L"float.__rtruediv__ expects a float receiver">;
    using FloatTrueDivHandlers =
        FloatTrueDivOperation::Handlers<FloatRaisingBinaryEffects,
                                        TrustedHandlerSemantics::TrueDiv>;
    using FloatRTrueDivHandlers =
        FloatRTrueDivOperation::Handlers<FloatRaisingBinaryEffects,
                                         TrustedHandlerSemantics::RTrueDiv>;
    using FloatTrueDivResolver =
        FloatBinaryResolver<FloatTrueDivHandlers, FloatRTrueDivHandlers>;
    using FloatRTrueDivResolver =
        FloatBinaryResolver<FloatRTrueDivHandlers, FloatTrueDivHandlers>;

    using FloatFloorDivOperation =
        FloatBinaryOperation<float_floor_divide,
                             L"float.__floordiv__ expects a float receiver">;
    using FloatRFloorDivOperation =
        FloatBinaryOperation<float_reverse_floor_divide,
                             L"float.__rfloordiv__ expects a float receiver">;
    using FloatFloorDivHandlers =
        FloatFloorDivOperation::Handlers<FloatRaisingBinaryEffects,
                                         TrustedHandlerSemantics::FloorDiv>;
    using FloatRFloorDivHandlers =
        FloatRFloorDivOperation::Handlers<FloatRaisingBinaryEffects,
                                          TrustedHandlerSemantics::RFloorDiv>;
    using FloatFloorDivResolver =
        FloatBinaryResolver<FloatFloorDivHandlers, FloatRFloorDivHandlers>;
    using FloatRFloorDivResolver =
        FloatBinaryResolver<FloatRFloorDivHandlers, FloatFloorDivHandlers>;

    using FloatModOperation =
        FloatBinaryOperation<float_modulo,
                             L"float.__mod__ expects a float receiver">;
    using FloatRModOperation =
        FloatBinaryOperation<float_reverse_modulo,
                             L"float.__rmod__ expects a float receiver">;
    using FloatModHandlers =
        FloatModOperation::Handlers<FloatRaisingBinaryEffects,
                                    TrustedHandlerSemantics::Mod>;
    using FloatRModHandlers =
        FloatRModOperation::Handlers<FloatRaisingBinaryEffects,
                                     TrustedHandlerSemantics::RMod>;
    using FloatModResolver =
        FloatBinaryResolver<FloatModHandlers, FloatRModHandlers>;
    using FloatRModResolver =
        FloatBinaryResolver<FloatRModHandlers, FloatModHandlers>;

    template <OrderingOp Operation>
    static Value float_order_result(std::partial_ordering comparison)
    {
        bool result;
        if constexpr(Operation == OrderingOp::Less)
            result = comparison < 0;
        else if constexpr(Operation == OrderingOp::LessEqual)
            result = comparison <= 0;
        else if constexpr(Operation == OrderingOp::Greater)
            result = comparison > 0;
        else
            result = comparison >= 0;
        return result ? Value::True() : Value::False();
    }

    template <OrderingOp Operation, FloatBinaryAdaptation Adaptation>
    static Value trusted_float_ordering(ThreadState *thread, Value left,
                                        Value right)
    {
        (void)thread;
        if constexpr(Adaptation == FloatBinaryAdaptation::FloatFloat)
            return float_order_result<Operation>(
                left.get_ptr<Float>()->value() <=>
                right.get_ptr<Float>()->value());
        else if constexpr(Adaptation == FloatBinaryAdaptation::FloatIntlike)
            return float_order_result<Operation>(
                compare_float_intlike(left.get_ptr<Float>()->value(), right));
        else
            return float_order_result<Operation>(
                0 <=>
                compare_float_intlike(right.get_ptr<Float>()->value(), left));
    }

    template <OrderingOp Operation, FixedWideString ReceiverError>
    struct FloatOrderingOperation
    {
        [[nodiscard]] static Value native(ThreadState *thread, Value self,
                                          Value other)
        {
            if(!has_native_layout<Float>(self))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", ReceiverError.c_str());
            double number = self.get_ptr<Float>()->value();
            if(has_native_layout<Float>(other))
                return float_order_result<Operation>(
                    number <=> other.get_ptr<Float>()->value());
            if(is_intlike_value(other))
            {
                // As for equality, builtin methods compare subclass payloads.
                // Only ordinary operator dispatch invokes user overrides.
                return float_order_result<Operation>(
                    compare_float_intlike(number, other));
            }
            return Value::NotImplemented();
        }

        template <TrustedHandlerEffects Effects,
                  TrustedHandlerSemantics Semantics>
        struct Handlers
        {
            using FloatFloat = TrustedHandlerDefinition<
                trusted_float_ordering<Operation,
                                       FloatBinaryAdaptation::FloatFloat>,
                Effects, Semantics>;
            using FloatIntlike = TrustedHandlerDefinition<
                trusted_float_ordering<Operation,
                                       FloatBinaryAdaptation::FloatIntlike>,
                Effects, Semantics>;
            using IntlikeFloat = TrustedHandlerDefinition<
                trusted_float_ordering<Operation,
                                       FloatBinaryAdaptation::IntlikeFloat>,
                Effects, Semantics>;
        };
    };

    using FloatLtOperation =
        FloatOrderingOperation<OrderingOp::Less,
                               L"float.__lt__ expects a float receiver">;
    using FloatLeOperation =
        FloatOrderingOperation<OrderingOp::LessEqual,
                               L"float.__le__ expects a float receiver">;
    using FloatGtOperation =
        FloatOrderingOperation<OrderingOp::Greater,
                               L"float.__gt__ expects a float receiver">;
    using FloatGeOperation =
        FloatOrderingOperation<OrderingOp::GreaterEqual,
                               L"float.__ge__ expects a float receiver">;
    using FloatLtHandlers =
        FloatLtOperation::Handlers<TrustedHandlerEffects::None,
                                   TrustedHandlerSemantics::Less>;
    using FloatLeHandlers =
        FloatLeOperation::Handlers<TrustedHandlerEffects::None,
                                   TrustedHandlerSemantics::LessEqual>;
    using FloatGtHandlers =
        FloatGtOperation::Handlers<TrustedHandlerEffects::None,
                                   TrustedHandlerSemantics::Greater>;
    using FloatGeHandlers =
        FloatGeOperation::Handlers<TrustedHandlerEffects::None,
                                   TrustedHandlerSemantics::GreaterEqual>;
    using FloatLtResolver =
        FloatBinaryResolver<FloatLtHandlers, FloatGtHandlers, true>;
    using FloatLeResolver =
        FloatBinaryResolver<FloatLeHandlers, FloatGeHandlers, true>;
    using FloatGtResolver =
        FloatBinaryResolver<FloatGtHandlers, FloatLtHandlers, true>;
    using FloatGeResolver =
        FloatBinaryResolver<FloatGeHandlers, FloatLeHandlers, true>;

    using FloatAbsOperation =
        FloatUnaryOperation<float_abs,
                            L"float.__abs__ expects a float receiver">;
    using FloatNegOperation =
        FloatUnaryOperation<float_negate,
                            L"float.__neg__ expects a float receiver">;
    struct FloatPosOperation
    {
        static Value native(ThreadState *thread, Value self)
        {
            if(!has_native_layout<Float>(self))
            {
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"float.__pos__ expects a float receiver");
            }

            Float *value = self.get_ptr<Float>();
            if(value->get_shape() ==
               thread->get_machine()->float_class()->get_instance_root_shape())
            {
                return self;
            }
            return thread->make_object_value<Float>(value->value()).raw_value();
        }

        static Value trusted(ThreadState *thread, Value value)
        {
            (void)thread;
            return value;
        }

        template <TrustedHandlerEffects Effects,
                  TrustedHandlerSemantics Semantics>
        using Handler = TrustedHandlerDefinition<trusted, Effects, Semantics>;
    };
    static TValue<SMI> float_hash_value(TValue<Float> self)
    {
        // CPython 3.14 uses 314159 as the infinity hash. Its
        // _Py_HashDouble processes the mantissa in 28-bit chunks:
        // https://github.com/python/cpython/blob/3.14/Python/pyhash.c
        constexpr int64_t infinity_hash = 314159;
        constexpr int mantissa_chunk_bits = 28;
        constexpr double mantissa_chunk_scale =
            static_cast<double>(uint64_t{1} << mantissa_chunk_bits);
        static_assert(clover_hash_modulus_bits > mantissa_chunk_bits);

        // Inspired by CPython's four-bit pointer rotation. Clover discards
        // those alignment bits and reduces with its existing hash modulus:
        // https://github.com/python/cpython/blob/3.14/Include/internal/pycore_pyhash.h
        constexpr int nan_pointer_alignment_bits = 4;

        double value = self.extract()->value();
        if(!std::isfinite(value))
        {
            if(std::isinf(value))
            {
                return TValue<SMI>::from_smi(value > 0 ? infinity_hash
                                                       : -infinity_hash);
            }
            // Live objects in the current heap have stable addresses. Distinct
            // NaNs should not all collide merely because they compare unequal.
            return canonicalize_nonnegative_raw_hash(
                reinterpret_cast<uintptr_t>(self.extract()) >>
                nan_pointer_alignment_bits);
        }

        // Preserve the existing SMI hash policy, including its lower endpoint.
        // The positive limit is exclusive: double(value_smi_max) rounds upward.
        if(value >= static_cast<double>(value_smi_min) &&
           value < -static_cast<double>(value_smi_min) &&
           std::trunc(value) == value)
        {
            int64_t hash = static_cast<int64_t>(value);
            return TValue<SMI>::from_smi(hash == -1 ? -2 : hash);
        }

        // Reduce the exact binary value modulo Clover's integer hash modulus.
        // As in CPython's _Py_HashDouble, powers of two become bit rotations
        // modulo the Mersenne modulus; no large integer needs to be allocated.
        int exponent;
        double mantissa = std::frexp(value, &exponent);
        int sign = mantissa < 0 ? -1 : 1;
        mantissa = std::fabs(mantissa);
        constexpr uint64_t modulus = static_cast<uint64_t>(clover_hash_modulus);
        uint64_t hash = 0;
        while(mantissa != 0)
        {
            hash = ((hash << mantissa_chunk_bits) & modulus) |
                   (hash >> (clover_hash_modulus_bits - mantissa_chunk_bits));
            mantissa *= mantissa_chunk_scale;
            exponent -= mantissa_chunk_bits;
            uint64_t digit = static_cast<uint64_t>(mantissa);
            mantissa -= static_cast<double>(digit);
            hash += digit;
            if(hash >= modulus)
            {
                hash -= modulus;
            }
        }
        int bits = static_cast<int>(clover_hash_modulus_bits);
        int rotation = exponent % bits;
        if(rotation < 0)
        {
            rotation += bits;
        }
        hash = ((hash << rotation) & modulus) | (hash >> (bits - rotation));
        int64_t result = static_cast<int64_t>(hash) * sign;
        return TValue<SMI>::from_smi(result == -1 ? -2 : result);
    }

    static Value native_float_hash(ThreadState *thread, Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.__hash__ expects a float receiver");
        }
        return float_hash_value(TValue<Float>::from_value_assumed(self))
            .raw_value();
    }

    static Value trusted_float_hash(ThreadState *thread, Value self)
    {
        (void)thread;
        return float_hash_value(TValue<Float>::from_value_assumed(self))
            .raw_value();
    }

    static Value float_bool_value(TValue<Float> self)
    {
        return self.extract()->value() != 0.0 ? Value::True() : Value::False();
    }

    static Value native_float_bool(ThreadState *thread, Value self)
    {
        if(!has_native_layout<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"float.__bool__ expects a float receiver");
        }
        return float_bool_value(TValue<Float>::from_value_assumed(self));
    }

    static Value trusted_float_bool(ThreadState *thread, Value self)
    {
        (void)thread;
        return float_bool_value(TValue<Float>::from_value_assumed(self));
    }

    using FloatBoolHandler =
        TrustedHandlerDefinition<trusted_float_bool,
                                 TrustedHandlerEffects::None>;

    using FloatHashHandler =
        TrustedHandlerDefinition<trusted_float_hash,
                                 TrustedHandlerEffects::None,
                                 TrustedHandlerSemantics::Generic>;

    using FloatAbsHandler =
        FloatAbsOperation::Handler<TrustedHandlerEffects::Allocate,
                                   TrustedHandlerSemantics::Generic>;
    using FloatNegHandler =
        FloatNegOperation::Handler<TrustedHandlerEffects::Allocate,
                                   TrustedHandlerSemantics::Neg>;
    using FloatPosHandler =
        FloatPosOperation::Handler<TrustedHandlerEffects::None,
                                   TrustedHandlerSemantics::Pos>;

    template <typename HandlerDefinition>
    class FloatUnaryResolver final
        : public TrustedHandlerResolverBase<HandlerDefinition>
    {
        using Base = TrustedHandlerResolverBase<HandlerDefinition>;
        using Handler = typename Base::template Handler<0>;

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
            ShapeKey float_key = ShapeKey::from_shape(
                vm->float_class()->get_instance_root_shape());
            if(operand0_key == float_key)
            {
                return Handler::resolution();
            }
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
    };

    using FloatBoolResolver = FloatUnaryResolver<FloatBoolHandler>;
    using FloatHashResolver = FloatUnaryResolver<FloatHashHandler>;
    using FloatAbsResolver = FloatUnaryResolver<FloatAbsHandler>;
    using FloatNegResolver = FloatUnaryResolver<FloatNegHandler>;
    using FloatPosResolver = FloatUnaryResolver<FloatPosHandler>;

    BuiltinClassDefinition make_float_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Float};
        ClassObject *cls = ClassObject::make_builtin_class<Float>(
            vm->get_or_create_interned_string_value(L"float"), 0, nullptr, 0,
            vm->object_class(), SubclassPolicy::Allowed,
            immutable_shape_flags());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_float_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> defaults(
            vm->get_default_thread()->make_object_value<Tuple>(1));
        defaults.extract()->initialize_item_unchecked(0, Value::from_smi(0));
        BuiltinIntrinsicMethod methods[] = {
            with_defaults(builtin_intrinsic_method(
                              L"__new__", native_float_new,
                              L"Create a float from a numeric value."),
                          defaults.value()),
            builtin_intrinsic_method(L"__str__", native_float_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_float_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__float__", native_float_float,
                                     L"Return self as an exact float."),
            builtin_intrinsic_method(L"__int__", native_float_int,
                                     L"Truncate self toward zero."),
            builtin_intrinsic_method(L"__trunc__", native_float_trunc,
                                     L"Truncate self toward zero."),
            builtin_intrinsic_method(L"__floor__", native_float_floor,
                                     L"Return the greatest integer <= self."),
            builtin_intrinsic_method(L"__ceil__", native_float_ceil,
                                     L"Return the least integer >= self."),
            builtin_intrinsic_method(
                L"is_integer", native_float_is_integer,
                L"Return whether self is a finite integer."),
            builtin_intrinsic_method(L"conjugate", native_float_conjugate,
                                     L"Return self as an exact float."),
            with_trusted_handler_resolver<FloatEqResolver>(
                vm,
                builtin_intrinsic_method(L"__eq__", FloatEqOperation::native,
                                         L"Return self == value.")),
            with_trusted_handler_resolver<FloatNeResolver>(
                vm,
                builtin_intrinsic_method(L"__ne__", FloatNeOperation::native,
                                         L"Return self != value.")),
            with_trusted_handler_resolver<FloatAddResolver>(
                vm,
                builtin_intrinsic_method(L"__add__", FloatAddOperation::native,
                                         L"Return self + value.")),
            with_trusted_handler_resolver<FloatRAddResolver>(
                vm, builtin_intrinsic_method(L"__radd__",
                                             FloatRAddOperation::native,
                                             L"Return value + self.")),
            with_trusted_handler_resolver<FloatSubResolver>(
                vm,
                builtin_intrinsic_method(L"__sub__", FloatSubOperation::native,
                                         L"Return self - value.")),
            with_trusted_handler_resolver<FloatRSubResolver>(
                vm, builtin_intrinsic_method(L"__rsub__",
                                             FloatRSubOperation::native,
                                             L"Return value - self.")),
            with_trusted_handler_resolver<FloatMulResolver>(
                vm,
                builtin_intrinsic_method(L"__mul__", FloatMulOperation::native,
                                         L"Return self * value.")),
            with_trusted_handler_resolver<FloatRMulResolver>(
                vm, builtin_intrinsic_method(L"__rmul__",
                                             FloatRMulOperation::native,
                                             L"Return value * self.")),
            with_trusted_handler_resolver<FloatTrueDivResolver>(
                vm, builtin_intrinsic_method(L"__truediv__",
                                             FloatTrueDivOperation::native,
                                             L"Return self / value.")),
            with_trusted_handler_resolver<FloatRTrueDivResolver>(
                vm, builtin_intrinsic_method(L"__rtruediv__",
                                             FloatRTrueDivOperation::native,
                                             L"Return value / self.")),
            with_trusted_handler_resolver<FloatFloorDivResolver>(
                vm, builtin_intrinsic_method(L"__floordiv__",
                                             FloatFloorDivOperation::native,
                                             L"Return self // value.")),
            with_trusted_handler_resolver<FloatRFloorDivResolver>(
                vm, builtin_intrinsic_method(L"__rfloordiv__",
                                             FloatRFloorDivOperation::native,
                                             L"Return value // self.")),
            with_trusted_handler_resolver<FloatModResolver>(
                vm,
                builtin_intrinsic_method(L"__mod__", FloatModOperation::native,
                                         L"Return self % value.")),
            with_trusted_handler_resolver<FloatRModResolver>(
                vm, builtin_intrinsic_method(L"__rmod__",
                                             FloatRModOperation::native,
                                             L"Return value % self.")),
            with_trusted_handler_resolver<FloatLtResolver>(
                vm,
                builtin_intrinsic_method(L"__lt__", FloatLtOperation::native,
                                         L"Return self < value.")),
            with_trusted_handler_resolver<FloatLeResolver>(
                vm,
                builtin_intrinsic_method(L"__le__", FloatLeOperation::native,
                                         L"Return self <= value.")),
            with_trusted_handler_resolver<FloatGtResolver>(
                vm,
                builtin_intrinsic_method(L"__gt__", FloatGtOperation::native,
                                         L"Return self > value.")),
            with_trusted_handler_resolver<FloatGeResolver>(
                vm,
                builtin_intrinsic_method(L"__ge__", FloatGeOperation::native,
                                         L"Return self >= value.")),
            with_trusted_handler_resolver<FloatBoolResolver>(
                vm, builtin_intrinsic_method(L"__bool__", native_float_bool,
                                             L"Return self != 0.")),
            with_trusted_handler_resolver<FloatHashResolver>(
                vm, builtin_intrinsic_method(L"__hash__", native_float_hash,
                                             L"Return hash(self).")),
            with_trusted_handler_resolver<FloatNegResolver>(
                vm,
                builtin_intrinsic_method(L"__neg__", FloatNegOperation::native,
                                         L"Return -self.")),
            with_trusted_handler_resolver<FloatAbsResolver>(
                vm,
                builtin_intrinsic_method(L"__abs__", FloatAbsOperation::native,
                                         L"Return abs(self).")),
            with_trusted_handler_resolver<FloatPosResolver>(
                vm,
                builtin_intrinsic_method(L"__pos__", FloatPosOperation::native,
                                         L"Return +self.")),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->float_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
