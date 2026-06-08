#include "builtin_types/float.h"

#include "builtin_types/str.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/shape_key.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
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

    static Value native_float_eq(ThreadState *thread, Value self, Value other)
    {
        (void)thread;
        if(!can_convert_to<Float>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"float.__eq__ expects a float receiver");
        }

        double right;
        if(!try_get_float_or_smi_or_bool(other, &right))
        {
            return Value::NotImplemented();
        }
        double left = self.get_ptr<Float>()->value;
        return left == right ? Value::True() : Value::False();
    }

    static double smi_or_bool_as_double(Value value)
    {
        assert((value.as.integer & value_not_smi_or_boolean_mask) == 0);
        Value integer_value;
        integer_value.as.integer =
            value.as.integer & value_boolean_to_integer_mask;
        return static_cast<double>(integer_value.get_smi());
    }

    static Value trusted_float_eq_float_handler(ThreadState *thread,
                                                Value left_value,
                                                Value right_value)
    {
        (void)thread;
        double left = left_value.get_ptr<Float>()->value;
        double right = right_value.get_ptr<Float>()->value;
        return left == right ? Value::True() : Value::False();
    }

    static Value trusted_float_eq_intlike_handler(ThreadState *thread,
                                                  Value left_value,
                                                  Value right_value)
    {
        (void)thread;
        double left = left_value.get_ptr<Float>()->value;
        double right = smi_or_bool_as_double(right_value);
        return left == right ? Value::True() : Value::False();
    }

    static bool is_smi_or_bool_shape_key(ShapeKey key)
    {
        return key == ShapeKey::from_value(Value::from_smi(0)) ||
               key == ShapeKey::from_value(Value::False());
    }

    static TrustedHandler
    resolve_trusted_float_eq_resolver(VirtualMachine *vm, ShapeKey operand0_key,
                                      ShapeKey operand1_key, ShapeKey unused)
    {
        (void)unused;

        ShapeKey float_key =
            ShapeKey::from_shape(vm->float_class()->get_instance_root_shape());
        if(operand0_key == float_key)
        {
            if(operand1_key == float_key)
            {
                return TrustedHandler::for_binary(
                    trusted_float_eq_float_handler);
            }
            if(is_smi_or_bool_shape_key(operand1_key))
            {
                return TrustedHandler::for_binary(
                    trusted_float_eq_intlike_handler);
            }
        }
        return TrustedHandler::none();
    }

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
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__eq__", native_float_eq,
                                         L"Return self == value."),
                resolve_trusted_float_eq_resolver),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->float_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
