#ifndef CL_PROTOCOL_HELPER_FUNCTIONS_H
#define CL_PROTOCOL_HELPER_FUNCTIONS_H

#include "object_model/function.h"
#include "object_model/typed_value.h"

namespace cl
{
    class VirtualMachine;

    Expected<TValue<Function>>
    make_hash_value_helper_function(VirtualMachine *vm);

    Expected<TValue<Function>>
    make_test_equal_helper_function(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_PROTOCOL_HELPER_FUNCTIONS_H
