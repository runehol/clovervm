#include "bool.h"

#include "class_object.h"
#include "native_function.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    static Value native_bool_str(Value self)
    {
        if(!self.is_bool())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"bool.__str__ expects a bool receiver");
        }
        return active_thread()
            ->make_object_value<String>(self == Value::True() ? L"True"
                                                              : L"False")
            .raw_value();
    }

    BuiltinClassDefinition make_bool_class(VirtualMachine *vm,
                                           ClassObject *int_class)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"bool"), 0, nullptr, 0,
            int_class, NativeLayoutId::Invalid);
        return builtin_class_definition(cls);
    }

    void install_bool_class_methods(VirtualMachine *vm)
    {
        BuiltinNativeMethod methods[] = {
            builtin_native_method(L"__str__", native_bool_str,
                                  L"Return str(self)."),
            builtin_native_method(L"__repr__", native_bool_str,
                                  L"Return repr(self)."),
        };
        install_builtin_native_methods(vm, vm->bool_class(), methods,
                                       std::size(methods));
    }

}  // namespace cl
