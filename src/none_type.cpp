#include "none_type.h"

#include "class_object.h"
#include "native_function.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    static Value native_none_type_str(ThreadState *thread, Value self)
    {
        if(!self.is_none())
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"NoneType.__str__ expects a NoneType receiver");
        }
        return active_thread()->make_object_value<String>(L"None").raw_value();
    }

    BuiltinClassDefinition make_none_type_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"NoneType"), 0, nullptr, 0,
            vm->object_class(), NativeLayoutId::Invalid);
        return builtin_class_definition(cls);
    }

    void install_none_type_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_none_type_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_none_type_str,
                                     L"Return repr(self)."),
        };
        install_builtin_intrinsic_methods(vm, vm->none_type_class(), methods,
                                          std::size(methods));
    }

}  // namespace cl
