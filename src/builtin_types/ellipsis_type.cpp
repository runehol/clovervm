#include "builtin_types/ellipsis_type.h"

#include "builtin_types/str.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <iterator>

namespace cl
{
    static Value native_ellipsis_type_str(ThreadState *thread, Value self)
    {
        if(!self.is_ellipsis_singleton())
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"ellipsis.__str__ expects an ellipsis receiver");
        }
        return thread->make_object_value<String>(L"Ellipsis").raw_value();
    }

    BuiltinClassDefinition make_ellipsis_type_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"ellipsis"), 0, nullptr, 0,
            vm->object_class(), NativeLayoutId::Invalid);
        return builtin_class_definition(cls, BuiltinsVisibility::Internal);
    }

    void install_ellipsis_type_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__str__", native_ellipsis_type_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_ellipsis_type_str,
                                     L"Return repr(self)."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->ellipsis_type_class(),
                                              methods, std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
