#include "int.h"

#include "class_object.h"
#include "native_function.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>
#include <string>

namespace cl
{
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

    BuiltinClassDefinition make_int_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"int"), 0, nullptr, 0,
            vm->object_class(), NativeLayoutId::Invalid);
        return builtin_class_definition(cls);
    }

    void install_int_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_int_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_int_str,
                                     L"Return repr(self)."),
        };
        install_builtin_intrinsic_methods(vm, vm->int_class(), methods,
                                          std::size(methods));
    }

}  // namespace cl
