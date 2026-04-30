#ifndef CL_NATIVE_FUNCTION_H
#define CL_NATIVE_FUNCTION_H

#include "code_object.h"
#include "function.h"
#include "owned_typed_value.h"

namespace cl
{
    class Tuple;
    class VirtualMachine;

    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction0 function,
                         TValue<Tuple> default_parameters =
                             TValue<Tuple>::unsafe_unchecked(Value::None()));
    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction1 function,
                         TValue<Tuple> default_parameters =
                             TValue<Tuple>::unsafe_unchecked(Value::None()));
    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction2 function,
                         TValue<Tuple> default_parameters =
                             TValue<Tuple>::unsafe_unchecked(Value::None()));
    TValue<Function>
    make_native_function(VirtualMachine *vm, NativeFunction3 function,
                         TValue<Tuple> default_parameters =
                             TValue<Tuple>::unsafe_unchecked(Value::None()));

}  // namespace cl

#endif  // CL_NATIVE_FUNCTION_H
