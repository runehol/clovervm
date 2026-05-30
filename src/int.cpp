#include "int.h"

#include "class_object.h"
#include "native_function.h"
#include "owned.h"
#include "str.h"
#include "thread_state.h"
#include "tuple.h"
#include "value.h"
#include "virtual_machine.h"
#include <iterator>
#include <string>

namespace cl
{
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

        return thread->set_pending_builtin_exception_string(
            L"TypeError",
            L"int conversion is only implemented for int and bool");
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
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->int_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
