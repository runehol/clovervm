#include "builtin_types/bool.h"

#include "builtin_types/str.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <iterator>

namespace cl
{
    static Value native_bool_str(ThreadState *thread, Value self)
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
        return builtin_class_definition(cls, BuiltinsVisibility::Public);
    }

    void install_bool_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_bool_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_bool_str,
                                     L"Return repr(self)."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->bool_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
