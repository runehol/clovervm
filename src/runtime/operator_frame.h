#ifndef CL_OPERATOR_FRAME_H
#define CL_OPERATOR_FRAME_H

#include "object_model/value.h"
#include "runtime/operator_dispatch.h"
#include <cassert>
#include <cstdint>

namespace cl
{
    [[maybe_unused]] static ALWAYSINLINE void
    read_operator_continuation_header(Value *fp, int32_t prefix_reg,
                                      OperatorDispatchTableId &table_id,
                                      uint32_t &resume_index)
    {
        Value table_id_value = fp[prefix_reg];
        Value resume_index_value = fp[prefix_reg - 1];
        assert(table_id_value.is_smi());
        assert(resume_index_value.is_smi());

        int64_t table_id_raw = table_id_value.get_smi();
        int64_t resume_index_raw = resume_index_value.get_smi();
        assert(table_id_raw >= 0);
        assert(table_id_raw <
               int64_t(static_cast<uint32_t>(OperatorDispatchTableId::Count)));
        assert(resume_index_raw >= 0);
        assert(resume_index_raw <= int64_t(UINT32_MAX));

        table_id = static_cast<OperatorDispatchTableId>(uint32_t(table_id_raw));
        resume_index = uint32_t(resume_index_raw);
    }

    [[maybe_unused]] static ALWAYSINLINE void
    write_operator_continuation_resume_index(Value *fp, int32_t prefix_reg,
                                             uint32_t resume_index)
    {
        fp[prefix_reg - 1] = Value::from_smi(int64_t(resume_index));
    }

    [[maybe_unused]] static ALWAYSINLINE void
    read_binary_operator_continuation_operands(Value *fp, int32_t prefix_reg,
                                               Value &operand0, Value &operand1)
    {
        operand0 = fp[prefix_reg - 2];
        operand1 = fp[prefix_reg - 3];
    }

    [[maybe_unused]] static ALWAYSINLINE void
    read_ternary_operator_continuation_operands(Value *fp, int32_t prefix_reg,
                                                Value &operand0,
                                                Value &operand1,
                                                Value &operand2)
    {
        operand0 = fp[prefix_reg - 2];
        operand1 = fp[prefix_reg - 3];
        operand2 = fp[prefix_reg - 4];
    }

    [[maybe_unused]] static ALWAYSINLINE void
    setup_unary_operator_continuation_prefix(Value *fp, int32_t prefix_reg,
                                             OperatorDispatchTableId table_id,
                                             uint32_t resume_index,
                                             Value operand0)
    {
        fp[prefix_reg] =
            Value::from_smi(int64_t(static_cast<uint32_t>(table_id)));
        fp[prefix_reg - 1] = Value::from_smi(int64_t(resume_index));
        fp[prefix_reg - 2] = operand0;
    }

    [[maybe_unused]] static ALWAYSINLINE int32_t setup_unary_operator_call_args(
        Value *fp, int32_t prefix_reg, Value operand0)
    {
        int32_t first_arg_reg = prefix_reg - 4;
        fp[first_arg_reg] = operand0;
        return first_arg_reg;
    }

    [[maybe_unused]] static ALWAYSINLINE void
    setup_binary_operator_continuation_prefix(Value *fp, int32_t prefix_reg,
                                              OperatorDispatchTableId table_id,
                                              uint32_t resume_index,
                                              Value operand0, Value operand1)
    {
        fp[prefix_reg] =
            Value::from_smi(int64_t(static_cast<uint32_t>(table_id)));
        fp[prefix_reg - 1] = Value::from_smi(int64_t(resume_index));
        fp[prefix_reg - 2] = operand0;
        fp[prefix_reg - 3] = operand1;
    }

    [[maybe_unused]] static ALWAYSINLINE void
    setup_ternary_operator_continuation_prefix(Value *fp, int32_t prefix_reg,
                                               OperatorDispatchTableId table_id,
                                               uint32_t resume_index,
                                               Value operand0, Value operand1,
                                               Value operand2)
    {
        fp[prefix_reg] =
            Value::from_smi(int64_t(static_cast<uint32_t>(table_id)));
        fp[prefix_reg - 1] = Value::from_smi(int64_t(resume_index));
        fp[prefix_reg - 2] = operand0;
        fp[prefix_reg - 3] = operand1;
        fp[prefix_reg - 4] = operand2;
    }

    [[maybe_unused]] static ALWAYSINLINE int32_t
    setup_ternary_operator_call_args(Value *fp, int32_t prefix_reg,
                                     Value operand0, Value operand1,
                                     Value operand2)
    {
        int32_t first_arg_reg = prefix_reg - 6;
        fp[first_arg_reg] = operand0;
        fp[first_arg_reg - 1] = operand1;
        fp[first_arg_reg - 2] = operand2;
        return first_arg_reg;
    }

}  // namespace cl

#endif
