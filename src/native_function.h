#ifndef CL_NATIVE_FUNCTION_H
#define CL_NATIVE_FUNCTION_H

#include "code_object.h"
#include "function.h"
#include "owned_typed_value.h"

namespace cl
{
    class VirtualMachine;

    TValue<Function> make_native_function(VirtualMachine *vm,
                                          NativeFunction0 function);
    TValue<Function> make_native_function(VirtualMachine *vm,
                                          NativeFunction1 function);
    TValue<Function> make_native_function(VirtualMachine *vm,
                                          NativeFunction2 function);

}  // namespace cl

#endif  // CL_NATIVE_FUNCTION_H
