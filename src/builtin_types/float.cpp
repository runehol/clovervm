#include "builtin_types/float.h"

#include "builtin_types/str.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/shape_key.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include "util/fixed_wide_string.h"
#include <cmath>
#include <fmt/format.h>
#include <iterator>
#include <string>

namespace cl
{
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
        if(!can_convert_to<Float>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"float.__str__ expects a float receiver");
        }
        return active_thread()
            ->make_object_value<String>(
                format_float_value(self.get_ptr<Float>()->value))
            .raw_value();
    }

    static Value native_float_repr(ThreadState *thread, Value self)
    {
        if(!can_convert_to<Float>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"float.__repr__ expects a float receiver");
        }
        return active_thread()
            ->make_object_value<String>(
                format_float_value(self.get_ptr<Float>()->value))
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
        if(can_convert_to<Float>(value))
        {
            *out = value.get_ptr<Float>()->value;
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

    static Value float_equal(ThreadState *thread, double left, double right)
    {
        (void)thread;
        return left == right ? Value::True() : Value::False();
    }

    struct FloatNeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__ne__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            (void)thread;
            return left != right ? Value::True() : Value::False();
        }
    };

    struct FloatAddOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__add__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            return thread->make_object_value<Float>(left + right).raw_value();
        }
    };

    struct FloatRAddOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__radd__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            return FloatAddOperator{}(thread, left, right);
        }
    };

    static Value float_subtract(ThreadState *thread, double left, double right)
    {
        return thread->make_object_value<Float>(left - right).raw_value();
    }

    static Value float_reverse_subtract(ThreadState *thread, double left,
                                        double right)
    {
        return thread->make_object_value<Float>(right - left).raw_value();
    }

    struct FloatMulOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__mul__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            return thread->make_object_value<Float>(left * right).raw_value();
        }
    };

    struct FloatRMulOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__rmul__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            return FloatMulOperator{}(thread, left, right);
        }
    };

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

    struct FloatTrueDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__truediv__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            if(unlikely(right == 0.0))
            {
                return float_zero_division_error(thread);
            }
            return thread->make_object_value<Float>(left / right).raw_value();
        }
    };

    struct FloatRTrueDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__rtruediv__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            if(unlikely(left == 0.0))
            {
                return float_zero_division_error(thread);
            }
            return thread->make_object_value<Float>(right / left).raw_value();
        }
    };

    struct FloatFloorDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__floordiv__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            if(unlikely(right == 0.0))
            {
                return float_zero_division_error(thread);
            }
            return thread
                ->make_object_value<Float>(float_divmod(left, right).quotient)
                .raw_value();
        }
    };

    struct FloatRFloorDivOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__rfloordiv__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            if(unlikely(left == 0.0))
            {
                return float_zero_division_error(thread);
            }
            return thread
                ->make_object_value<Float>(float_divmod(right, left).quotient)
                .raw_value();
        }
    };

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

    struct FloatModOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__mod__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            return float_modulo_result(thread, left, right);
        }
    };

    struct FloatRModOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__rmod__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            return float_modulo_result(thread, right, left);
        }
    };

    struct FloatLtOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__lt__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            (void)thread;
            return left < right ? Value::True() : Value::False();
        }
    };

    struct FloatLeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__le__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            (void)thread;
            return left <= right ? Value::True() : Value::False();
        }
    };

    struct FloatGtOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__gt__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            (void)thread;
            return left > right ? Value::True() : Value::False();
        }
    };

    struct FloatGeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__ge__ expects a float receiver";

        Value operator()(ThreadState *thread, double left, double right) const
        {
            (void)thread;
            return left >= right ? Value::True() : Value::False();
        }
    };

    struct FloatNegOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__neg__ expects a float receiver";

        Value operator()(ThreadState *thread, double value) const
        {
            return thread->make_object_value<Float>(-value).raw_value();
        }
    };

    struct FloatPosOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"float.__pos__ expects a float receiver";

        Value operator()(ThreadState *thread, double value) const
        {
            return thread->make_object_value<Float>(value).raw_value();
        }
    };

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
            if(!can_convert_to<Float>(self))
            {
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", ReceiverError.c_str());
            }

            double right;
            if(!try_get_float_or_smi_or_bool(other, &right))
            {
                return Value::NotImplemented();
            }
            return Function(thread, self.get_ptr<Float>()->value, right);
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
            return Function(thread, left.get_ptr<Float>()->value,
                            right.get_ptr<Float>()->value);
        }
        else if constexpr(Adaptation == FloatBinaryAdaptation::FloatIntlike)
        {
            return Function(thread, left.get_ptr<Float>()->value,
                            smi_or_bool_as_double(right));
        }
        else
        {
            static_assert(Adaptation == FloatBinaryAdaptation::IntlikeFloat);
            return Function(thread, smi_or_bool_as_double(left),
                            right.get_ptr<Float>()->value);
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

    template <typename Operator>
    static Value native_float_unary_operator(ThreadState *thread, Value self)
    {
        if(!can_convert_to<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }

        return Operator{}(thread, self.get_ptr<Float>()->value);
    }

    template <typename Operator>
    static Value native_float_binary_operator(ThreadState *thread, Value self,
                                              Value other)
    {
        if(!can_convert_to<Float>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }

        double right;
        if(!try_get_float_or_smi_or_bool(other, &right))
        {
            return Value::NotImplemented();
        }
        double left = self.get_ptr<Float>()->value;
        return Operator{}(thread, left, right);
    }

    template <typename Operator>
    static Value trusted_float_float_operator(ThreadState *thread,
                                              Value left_value,
                                              Value right_value)
    {
        return Operator{}(thread, left_value.get_ptr<Float>()->value,
                          right_value.get_ptr<Float>()->value);
    }

    template <typename Operator>
    static Value trusted_float_intlike_operator(ThreadState *thread,
                                                Value left_value,
                                                Value right_value)
    {
        return Operator{}(thread, left_value.get_ptr<Float>()->value,
                          smi_or_bool_as_double(right_value));
    }

    template <typename Operator>
    static Value trusted_intlike_float_operator(ThreadState *thread,
                                                Value left_value,
                                                Value right_value)
    {
        return Operator{}(thread, smi_or_bool_as_double(left_value),
                          right_value.get_ptr<Float>()->value);
    }

    template <typename Operator>
    static Value trusted_float_unary_operator(ThreadState *thread, Value value)
    {
        return Operator{}(thread, value.get_ptr<Float>()->value);
    }

    static bool is_smi_or_bool_shape_key(ShapeKey key)
    {
        return key == ShapeKey::from_value(Value::from_smi(0)) ||
               key == ShapeKey::from_value(Value::False());
    }

    template <typename NormalHandlers, typename ReflectedHandlers>
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
            if(operand0_key == float_key)
            {
                if(operand1_key == float_key)
                {
                    return FloatFloat::resolution();
                }
                if(is_smi_or_bool_shape_key(operand1_key))
                {
                    return FloatIntlike::resolution();
                }
            }
            if(is_smi_or_bool_shape_key(operand0_key) &&
               operand1_key == float_key)
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

    using FloatEqOperation =
        FloatBinaryOperation<float_equal,
                             L"float.__eq__ expects a float receiver">;
    using FloatEqHandlers =
        FloatEqOperation::Handlers<TrustedHandlerEffects::None,
                                   TrustedHandlerSemantics::Equal>;
    using FloatEqResolver =
        FloatBinaryResolver<FloatEqHandlers, FloatEqHandlers>;

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

    template <typename Operator>
    static TrustedResolution resolve_trusted_float_binary_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key)
    {
        ShapeKey float_key =
            ShapeKey::from_shape(vm->float_class()->get_instance_root_shape());
        if(operand0_key == float_key)
        {
            if(operand1_key == float_key)
            {
                return TrustedResolution::call_trusted(
                    trusted_float_float_operator<Operator>);
            }
            if(is_smi_or_bool_shape_key(operand1_key))
            {
                return TrustedResolution::call_trusted(
                    trusted_float_intlike_operator<Operator>);
            }
        }
        if(is_smi_or_bool_shape_key(operand0_key) && operand1_key == float_key)
        {
            return TrustedResolution::call_trusted(
                trusted_intlike_float_operator<Operator>);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename NormalOperator, typename ReflectedOperator>
    static TrustedResolution resolve_trusted_float_binary_resolver(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(order == TrustedHandlerOperandOrder::Reflected)
        {
            return resolve_trusted_float_binary_handler<ReflectedOperator>(
                vm, operand0_key, operand1_key);
        }
        return resolve_trusted_float_binary_handler<NormalOperator>(
            vm, operand0_key, operand1_key);
    }

    template <typename Operator, TrustedHandlerEffects Effects,
              TrustedHandlerSemantics Semantics>
    struct FloatUnaryHandler
        : TrustedHandlerDefinition<trusted_float_unary_operator<Operator>,
                                   Effects, Semantics>
    {
    };

    using FloatNegHandler =
        FloatUnaryHandler<FloatNegOperator, TrustedHandlerEffects::Allocate,
                          TrustedHandlerSemantics::Neg>;
    using FloatPosHandler =
        FloatUnaryHandler<FloatPosOperator, TrustedHandlerEffects::Allocate,
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

    BuiltinClassDefinition make_float_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Float};
        ClassObject *cls = ClassObject::make_builtin_class<Float>(
            vm->get_or_create_interned_string_value(L"float"),
            Float::native_static_release_count(), nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_float_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_float_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_float_repr,
                                     L"Return repr(self)."),
            with_trusted_handler_resolver<FloatEqResolver>(
                vm,
                builtin_intrinsic_method(L"__eq__", FloatEqOperation::native,
                                         L"Return self == value.")),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__ne__", native_float_binary_operator<FloatNeOperator>,
                    L"Return self != value."),
                resolve_trusted_float_binary_resolver<FloatNeOperator,
                                                      FloatNeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__add__", native_float_binary_operator<FloatAddOperator>,
                    L"Return self + value."),
                resolve_trusted_float_binary_resolver<FloatAddOperator,
                                                      FloatAddOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__radd__",
                    native_float_binary_operator<FloatRAddOperator>,
                    L"Return value + self."),
                resolve_trusted_float_binary_resolver<FloatRAddOperator,
                                                      FloatAddOperator>),
            with_trusted_handler_resolver<FloatSubResolver>(
                vm,
                builtin_intrinsic_method(L"__sub__", FloatSubOperation::native,
                                         L"Return self - value.")),
            with_trusted_handler_resolver<FloatRSubResolver>(
                vm, builtin_intrinsic_method(L"__rsub__",
                                             FloatRSubOperation::native,
                                             L"Return value - self.")),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__mul__", native_float_binary_operator<FloatMulOperator>,
                    L"Return self * value."),
                resolve_trusted_float_binary_resolver<FloatMulOperator,
                                                      FloatMulOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rmul__",
                    native_float_binary_operator<FloatRMulOperator>,
                    L"Return value * self."),
                resolve_trusted_float_binary_resolver<FloatRMulOperator,
                                                      FloatMulOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__truediv__",
                    native_float_binary_operator<FloatTrueDivOperator>,
                    L"Return self / value."),
                resolve_trusted_float_binary_resolver<FloatTrueDivOperator,
                                                      FloatRTrueDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rtruediv__",
                    native_float_binary_operator<FloatRTrueDivOperator>,
                    L"Return value / self."),
                resolve_trusted_float_binary_resolver<FloatRTrueDivOperator,
                                                      FloatTrueDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__floordiv__",
                    native_float_binary_operator<FloatFloorDivOperator>,
                    L"Return self // value."),
                resolve_trusted_float_binary_resolver<FloatFloorDivOperator,
                                                      FloatRFloorDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rfloordiv__",
                    native_float_binary_operator<FloatRFloorDivOperator>,
                    L"Return value // self."),
                resolve_trusted_float_binary_resolver<FloatRFloorDivOperator,
                                                      FloatFloorDivOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__mod__", native_float_binary_operator<FloatModOperator>,
                    L"Return self % value."),
                resolve_trusted_float_binary_resolver<FloatModOperator,
                                                      FloatRModOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__rmod__",
                    native_float_binary_operator<FloatRModOperator>,
                    L"Return value % self."),
                resolve_trusted_float_binary_resolver<FloatRModOperator,
                                                      FloatModOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__lt__", native_float_binary_operator<FloatLtOperator>,
                    L"Return self < value."),
                resolve_trusted_float_binary_resolver<FloatLtOperator,
                                                      FloatGtOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__le__", native_float_binary_operator<FloatLeOperator>,
                    L"Return self <= value."),
                resolve_trusted_float_binary_resolver<FloatLeOperator,
                                                      FloatGeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__gt__", native_float_binary_operator<FloatGtOperator>,
                    L"Return self > value."),
                resolve_trusted_float_binary_resolver<FloatGtOperator,
                                                      FloatLtOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__ge__", native_float_binary_operator<FloatGeOperator>,
                    L"Return self >= value."),
                resolve_trusted_float_binary_resolver<FloatGeOperator,
                                                      FloatLeOperator>),
            with_trusted_handler_resolver<FloatUnaryResolver<FloatNegHandler>>(
                vm,
                builtin_intrinsic_method(
                    L"__neg__", native_float_unary_operator<FloatNegOperator>,
                    L"Return -self.")),
            with_trusted_handler_resolver<FloatUnaryResolver<FloatPosHandler>>(
                vm,
                builtin_intrinsic_method(
                    L"__pos__", native_float_unary_operator<FloatPosOperator>,
                    L"Return +self.")),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->float_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
