#include "not_implemented_type.h"

#include "class_object.h"
#include "native_function.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>

namespace cl
{
    static Value native_not_implemented_type_str(ThreadState *thread,
                                                 Value self)
    {
        if(!self.is_not_implemented_singleton())
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"NotImplementedType.__str__ expects a "
                              L"NotImplementedType receiver");
        }
        return thread->make_object_value<String>(L"NotImplemented").raw_value();
    }

    BuiltinClassDefinition make_not_implemented_type_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"NotImplementedType"), 0,
            nullptr, 0, vm->object_class(), NativeLayoutId::Invalid);
        return builtin_class_definition(cls, BuiltinsVisibility::Internal);
    }

    void install_not_implemented_type_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__",
                                     native_not_implemented_type_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__",
                                     native_not_implemented_type_str,
                                     L"Return repr(self)."),
        };
        unwrap_bootstrap_expected(vm,
                                  install_builtin_intrinsic_methods(
                                      vm, vm->not_implemented_type_class(),
                                      methods, std::size(methods)),
                                  "installing intrinsic methods");
    }

}  // namespace cl
