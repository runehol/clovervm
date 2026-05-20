#ifndef CL_NATIVE_FUNCTION_H
#define CL_NATIVE_FUNCTION_H

#include "code_object.h"
#include "function.h"

namespace cl
{
    class ClassObject;
    class Tuple;
    class VirtualMachine;

    struct BuiltinNativeMethod
    {
        const wchar_t *name;
        NativeFunctionTarget target;
        uint32_t n_parameters;
        const wchar_t *doc;
    };

    BuiltinNativeMethod builtin_native_method(const wchar_t *name,
                                              NativeFunction0 function,
                                              const wchar_t *doc = nullptr);
    BuiltinNativeMethod builtin_native_method(const wchar_t *name,
                                              NativeFunction1 function,
                                              const wchar_t *doc = nullptr);
    BuiltinNativeMethod builtin_native_method(const wchar_t *name,
                                              NativeFunction2 function,
                                              const wchar_t *doc = nullptr);
    BuiltinNativeMethod builtin_native_method(const wchar_t *name,
                                              NativeFunction3 function,
                                              const wchar_t *doc = nullptr);

    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction0 function,
                         Optional<TValue2<Tuple>> default_parameters =
                             Optional<TValue2<Tuple>>::none());
    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction1 function,
                         Optional<TValue2<Tuple>> default_parameters =
                             Optional<TValue2<Tuple>>::none());
    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction2 function,
                         Optional<TValue2<Tuple>> default_parameters =
                             Optional<TValue2<Tuple>>::none());
    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction3 function,
                         Optional<TValue2<Tuple>> default_parameters =
                             Optional<TValue2<Tuple>>::none());
    TValue<Function> make_native_function(VirtualMachine *vm,
                                          const BuiltinNativeMethod &method);

    void install_builtin_native_methods(VirtualMachine *vm, ClassObject *cls,
                                        const BuiltinNativeMethod *methods,
                                        uint32_t method_count);

}  // namespace cl

#endif  // CL_NATIVE_FUNCTION_H
