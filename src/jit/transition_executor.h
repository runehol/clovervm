#ifndef CL_JIT_TRANSITION_EXECUTOR_H
#define CL_JIT_TRANSITION_EXECUTOR_H

#include "jit/transition_program.h"
#include "object_model/value.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace cl::jit
{
    struct InterpreterResumeState
    {
        Value accumulator;
        const uint8_t *pc;
        CodeObject *code_object;
    };

    static_assert(std::is_standard_layout_v<InterpreterResumeState>);
    static_assert(offsetof(InterpreterResumeState, accumulator) == 0);
    static_assert(offsetof(InterpreterResumeState, pc) == 8);
    static_assert(offsetof(InterpreterResumeState, code_object) == 16);
    static_assert(sizeof(InterpreterResumeState) == 24);

    class TransitionExecutionContext;

    extern "C" const InterpreterResumeState *
    cl_execute_transition_program(TransitionExecutionContext *context,
                                  const TransitionInstruction *program,
                                  Value *frame_pointer);

    class alignas(16) TransitionExecutionContext
    {
    public:
        static constexpr size_t RegisterFileSlotCount = 64;

        std::span<uint64_t> register_file() { return register_file_; }
        std::span<const uint64_t> register_file() const
        {
            return register_file_;
        }

        std::span<uint64_t> ensure_scratch(size_t slot_count)
        {
            if(scratch_.size() < slot_count)
            {
                scratch_.resize(slot_count);
            }
            return {scratch_.data(), slot_count};
        }

        static constexpr size_t register_file_offset();

    private:
        friend const InterpreterResumeState *
        cl_execute_transition_program(TransitionExecutionContext *,
                                      const TransitionInstruction *, Value *);

        std::array<uint64_t, RegisterFileSlotCount> register_file_{};
        InterpreterResumeState interpreter_resume_state_{};
        std::vector<uint64_t> scratch_;
    };

    constexpr size_t TransitionExecutionContext::register_file_offset()
    {
        return offsetof(TransitionExecutionContext, register_file_);
    }

    static_assert(std::is_standard_layout_v<TransitionExecutionContext>);
    static_assert(TransitionExecutionContext::register_file_offset() == 0);

}  // namespace cl::jit

#endif  // CL_JIT_TRANSITION_EXECUTOR_H
