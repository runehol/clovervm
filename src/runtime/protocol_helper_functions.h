#ifndef CL_PROTOCOL_HELPER_FUNCTIONS_H
#define CL_PROTOCOL_HELPER_FUNCTIONS_H

#include "object_model/function.h"
#include "object_model/typed_value.h"
#include "runtime/ordering_op.h"

namespace cl
{
    class AttributeMutationPlan;
    class ThreadState;
    class VirtualMachine;

    [[nodiscard]] Expected<void>
    execute_descriptor_mutation(ThreadState &thread,
                                const AttributeMutationPlan &plan,
                                Value receiver, Value rhs);

    Expected<TValue<Function>>
    make_hash_value_helper_function(VirtualMachine *vm);

    Expected<TValue<Function>>
    make_test_equal_helper_function(VirtualMachine *vm);

    Expected<TValue<Function>>
    make_ordering_helper_function(VirtualMachine *vm, OrderingOp operation);

}  // namespace cl

#endif  // CL_PROTOCOL_HELPER_FUNCTIONS_H
