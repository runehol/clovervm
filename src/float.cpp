#include "float.h"

#include "class_object.h"
#include "native_function.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
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

    static Value native_float_str(Value self)
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

    static Value native_float_repr(Value self)
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

    BuiltinClassDefinition make_float_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Float};
        ClassObject *cls = ClassObject::make_builtin_class<Float>(
            vm->get_or_create_interned_string_value(L"float"),
            Float::native_static_release_count(), nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
    }

    void install_float_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_float_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_float_repr,
                                     L"Return repr(self)."),
        };
        install_builtin_intrinsic_methods(vm, vm->float_class(), methods,
                                          std::size(methods));
    }

}  // namespace cl
