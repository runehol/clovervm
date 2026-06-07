#ifndef CL_NATIVE_FUNCTION_H
#define CL_NATIVE_FUNCTION_H

#include "bytecode/code_object.h"
#include "object_model/function.h"
#include "object_model/typed_value.h"

namespace cl
{
    class ClassObject;
    class Tuple;
    class VirtualMachine;

    enum class NativeFunctionParameterMode
    {
        FixedArity,
        VarArgs,
    };

    struct BuiltinIntrinsicMethod
    {
        const wchar_t *name;
        NativeFunctionTarget target;
        uint32_t n_parameters;
        const wchar_t *doc;
        Optional<TValue<Tuple>> default_parameters;
        NativeFunctionParameterMode parameter_mode =
            NativeFunctionParameterMode::FixedArity;
        TrustedHandlerResolver trusted_handler_resolver = nullptr;
    };

    BuiltinIntrinsicMethod
    builtin_intrinsic_method(const wchar_t *name, IntrinsicFunction0 function,
                             const wchar_t *doc = nullptr);
    BuiltinIntrinsicMethod
    builtin_intrinsic_method(const wchar_t *name, IntrinsicFunction1 function,
                             const wchar_t *doc = nullptr);
    BuiltinIntrinsicMethod
    builtin_intrinsic_method(const wchar_t *name, IntrinsicFunction2 function,
                             const wchar_t *doc = nullptr);
    BuiltinIntrinsicMethod
    builtin_intrinsic_method(const wchar_t *name, IntrinsicFunction3 function,
                             const wchar_t *doc = nullptr);
    BuiltinIntrinsicMethod
    builtin_intrinsic_method(const wchar_t *name, IntrinsicFunction4 function,
                             const wchar_t *doc = nullptr);
    BuiltinIntrinsicMethod
    builtin_intrinsic_method(const wchar_t *name, IntrinsicFunction5 function,
                             const wchar_t *doc = nullptr);
    BuiltinIntrinsicMethod
    builtin_intrinsic_method(const wchar_t *name, IntrinsicFunction6 function,
                             const wchar_t *doc = nullptr);
    BuiltinIntrinsicMethod
    builtin_intrinsic_method(const wchar_t *name, IntrinsicFunction7 function,
                             const wchar_t *doc = nullptr);
    BuiltinIntrinsicMethod with_defaults(BuiltinIntrinsicMethod method,
                                         TValue<Tuple> defaults);
    BuiltinIntrinsicMethod with_varargs(BuiltinIntrinsicMethod method);
    BuiltinIntrinsicMethod
    with_trusted_handler_resolver(BuiltinIntrinsicMethod method,
                                  TrustedHandlerResolver resolver);

    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction0 function,
                            Optional<TValue<Tuple>> default_parameters =
                                Optional<TValue<Tuple>>::none());
    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction1 function,
                            Optional<TValue<Tuple>> default_parameters =
                                Optional<TValue<Tuple>>::none());
    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction2 function,
                            Optional<TValue<Tuple>> default_parameters =
                                Optional<TValue<Tuple>>::none());
    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction3 function,
                            Optional<TValue<Tuple>> default_parameters =
                                Optional<TValue<Tuple>>::none());
    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction4 function,
                            Optional<TValue<Tuple>> default_parameters =
                                Optional<TValue<Tuple>>::none());
    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction5 function,
                            Optional<TValue<Tuple>> default_parameters =
                                Optional<TValue<Tuple>>::none());
    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction6 function,
                            Optional<TValue<Tuple>> default_parameters =
                                Optional<TValue<Tuple>>::none());
    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm, IntrinsicFunction7 function,
                            Optional<TValue<Tuple>> default_parameters =
                                Optional<TValue<Tuple>>::none());
    Expected<TValue<Function>>
    make_intrinsic_function(VirtualMachine *vm,
                            const BuiltinIntrinsicMethod &method);
    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_0 function,
                            Optional<TValue<String>> docstring);
    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_1 function,
                            Optional<TValue<String>> docstring);
    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_2 function,
                            Optional<TValue<String>> docstring);
    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_3 function,
                            Optional<TValue<String>> docstring);
    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_4 function,
                            Optional<TValue<String>> docstring);
    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_5 function,
                            Optional<TValue<String>> docstring);
    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_6 function,
                            Optional<TValue<String>> docstring);
    Expected<TValue<Function>>
    make_extension_function(VirtualMachine *vm, TValue<String> name,
                            clover_extension_fn_7 function,
                            Optional<TValue<String>> docstring);

    Expected<void>
    install_builtin_intrinsic_methods(VirtualMachine *vm, ClassObject *cls,
                                      const BuiltinIntrinsicMethod *methods,
                                      uint32_t method_count);

}  // namespace cl

#endif  // CL_NATIVE_FUNCTION_H
