#ifndef CL_JIT_TRANSITION_EXECUTOR_H
#define CL_JIT_TRANSITION_EXECUTOR_H

#include "jit/transition_program.h"
#include "object_model/value.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cl::jit
{
    class TransitionExecutionContext
    {
    public:
        std::span<uint64_t> ensure_scratch(size_t slot_count)
        {
            if(scratch_.size() < slot_count)
            {
                scratch_.resize(slot_count);
            }
            return {scratch_.data(), slot_count};
        }

    private:
        std::vector<uint64_t> scratch_;
    };

    struct TransitionExecutionInput
    {
        std::span<const uint64_t> register_file;
        Value *frame_pointer;
    };

    struct InterpreterResumeState
    {
        Value accumulator;
        CodeObject *code_object;
        BytecodePCOffset resume_pc_offset;
    };

    InterpreterResumeState
    execute_transition_program(TransitionExecutionContext &context,
                               const TransitionInstruction *program,
                               TransitionExecutionInput input);

}  // namespace cl::jit

#endif  // CL_JIT_TRANSITION_EXECUTOR_H
