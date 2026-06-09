#ifndef CL_OPERATOR_WALK_H
#define CL_OPERATOR_WALK_H

#include "bytecode/code_object.h"
#include "object_model/attribute_descriptor.h"
#include "object_model/shape_key.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include "runtime/operator_dispatch.h"
#include <cstdint>

namespace cl
{
    class Function;
    class ThreadState;

    enum class OperatorOperandOrder
    {
        Normal,
        Reflected,
    };

    enum class OperatorWalkStatus
    {
        CallPythonFunction,
        CallTrustedHandler,
        NativeResult,
        PropagatePendingException,
    };

    struct OperatorWalkDescriptor
    {
        OperatorWalkStatus status =
            OperatorWalkStatus::PropagatePendingException;
        uint32_t resume_index = 0;
        OperatorStepAction action = OperatorStepAction::IdentityEq;
        OperatorOperandOrder operand_order = OperatorOperandOrder::Normal;
        Value result = Value::None();
        OperatorInlineCache cache_entry;

        static OperatorWalkDescriptor native_result(Value result);
        static OperatorWalkDescriptor propagate_pending_exception();
        static OperatorWalkDescriptor call_python_function(
            OperatorStepAction action, uint32_t resume_index,
            OperatorOperandOrder operand_order, ShapeKey operand0_shape_key,
            ShapeKey operand1_shape_key, ShapeKey operand2_shape_key,
            TValue<Function> function, uint32_t n_args,
            FunctionCallAdaptation adaptation, bool has_self,
            ValidityCell *operand0_lookup_validity_cell,
            ValidityCell *operand1_lookup_validity_cell);
        static OperatorWalkDescriptor call_trusted_handler(
            OperatorStepAction action, ShapeKey operand0_shape_key,
            ShapeKey operand1_shape_key, ShapeKey operand2_shape_key,
            TrustedHandler handler, ValidityCell *operand0_lookup_validity_cell,
            ValidityCell *operand1_lookup_validity_cell);
    };

    OperatorWalkDescriptor walk_operator_table(ThreadState *thread,
                                               OperatorDispatchTableId table_id,
                                               uint32_t start_index,
                                               Value operand0, Value operand1);

}  // namespace cl

#endif
