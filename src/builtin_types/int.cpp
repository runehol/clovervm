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

namespace cl
{
    static Value invalid_int_literal(ThreadState *thread)
    {
        return thread->set_pending_builtin_exception_string(
            L"ValueError", L"invalid literal for int()");
    }

    static bool is_ascii_digit(cl_wchar ch) { return ch >= L'0' && ch <= L'9'; }

    static Value parse_int_string(ThreadState *thread, TValue<String> string)
    {
        String *str = string.extract();
        size_t begin = 0;
        size_t end = size_t(str->count.extract());
        while(begin < end && std::iswspace(str->data[begin]))
        {
            ++begin;
        }
        while(begin < end && std::iswspace(str->data[end - 1]))
        {
            --end;
        }
        if(begin == end)
        {
            return invalid_int_literal(thread);
        }

        bool negative = false;
        if(str->data[begin] == L'+' || str->data[begin] == L'-')
        {
            negative = str->data[begin] == L'-';
            ++begin;
        }

        static constexpr uint64_t positive_limit =
            static_cast<uint64_t>(value_smi_max);
        static constexpr uint64_t negative_limit = positive_limit + 1;
        uint64_t limit = negative ? negative_limit : positive_limit;
        uint64_t parsed = 0;
        bool saw_digit = false;
        bool previous_underscore = false;
        for(size_t idx = begin; idx < end; ++idx)
        {
            cl_wchar ch = str->data[idx];
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

            uint64_t digit = static_cast<uint64_t>(ch - L'0');
            if(parsed > (limit - digit) / 10)
            {
                return thread->set_pending_builtin_exception_string(
                    L"OverflowError", L"integer overflow");
            }
            parsed = parsed * 10 + digit;
            saw_digit = true;
            previous_underscore = false;
        }
        if(!saw_digit || previous_underscore)
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
        if(!self.is_smi())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"int.__str__ expects an int receiver");
        }
        return active_thread()
            ->make_object_value<String>(std::to_wstring(self.get_smi()))
            .raw_value();
    }

    static bool try_get_smi_or_bool(Value value, int64_t *out)
    {
        if(unlikely((value.as.integer & value_not_smi_or_boolean_mask) == 0))
        {
            Value integer_value;
            integer_value.as.integer =
                value.as.integer & value_boolean_to_integer_mask;
            *out = integer_value.get_smi();
            return true;
        }
        return false;
    }

    static int64_t smi_or_bool_as_int(Value value)
    {
        assert((value.as.integer & value_not_smi_or_boolean_mask) == 0);
        Value integer_value;
        integer_value.as.integer =
            value.as.integer & value_boolean_to_integer_mask;
        return integer_value.get_smi();
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
    static TrustedResolution
    resolve_trusted_int_binary_handler(VirtualMachine *vm,
                                       ShapeKey operand0_key,
                                       ShapeKey operand1_key, ShapeKey unused)
    {
        (void)unused;

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
        ShapeKey operand2_key, TrustedHandlerOperandOrder order)
    {
        if(order == TrustedHandlerOperandOrder::Reflected)
        {
            return resolve_trusted_int_binary_handler<ReflectedOperator>(
                vm, operand0_key, operand1_key, operand2_key);
        }
        return resolve_trusted_int_binary_handler<NormalOperator>(
            vm, operand0_key, operand1_key, operand2_key);
    }

    template <typename Operator>
    static TrustedResolution resolve_trusted_int_unary_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        ShapeKey operand2_key, TrustedHandlerOperandOrder order)
    {
        (void)vm;
        (void)operand1_key;
        (void)operand2_key;
        (void)order;

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
        return builtin_class_definition(cls, BuiltinsVisibility::Public);
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
                    L"__neg__", native_int_unary_operator<SMINegOperator>,
                    L"Return -self."),
                resolve_trusted_int_unary_handler<SMINegOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__pos__", native_int_unary_operator<SMIPosOperator>,
                    L"Return +self."),
                resolve_trusted_int_unary_handler<SMIPosOperator>),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->int_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
