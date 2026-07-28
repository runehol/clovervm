#include "jit/transition_executor.h"

#include "runtime/fatal.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace cl::jit
{
    namespace
    {
        static_assert(sizeof(Value) == sizeof(uint64_t));

        uint64_t load_stack_word(Value *frame_pointer, int16_t offset)
        {
            uint64_t result;
            std::memcpy(&result, frame_pointer + offset, sizeof(result));
            return result;
        }

        void store_stack_word(Value *frame_pointer, int16_t offset,
                              uint64_t value)
        {
            std::memcpy(frame_pointer + offset, &value, sizeof(value));
        }

        Value value_from_word(uint64_t word)
        {
            Value result;
            std::memcpy(&result, &word, sizeof(result));
            return result;
        }

        ThreadState *thread_state_from_word(uint64_t word)
        {
            static_assert(sizeof(ThreadState *) == sizeof(word));
            ThreadState *result;
            std::memcpy(&result, &word, sizeof(result));
            return result;
        }

        uint64_t read_location(TransitionLocation location,
                               TransitionExecutionInput input,
                               std::span<uint64_t> scratch)
        {
            switch(location.area())
            {
                case TransitionLocationArea::RegisterFile:
                    {
                        size_t offset = static_cast<size_t>(location.offset());
                        assert(offset < input.register_file.size());
                        return input.register_file[offset];
                    }
                case TransitionLocationArea::Stack:
                    return load_stack_word(input.frame_pointer,
                                           location.offset());
                case TransitionLocationArea::Scratch:
                    {
                        size_t offset = static_cast<size_t>(location.offset());
                        assert(offset < scratch.size());
                        return scratch[offset];
                    }
            }
            fatal("invalid transition location area");
        }

        void write_location(TransitionLocation location, uint64_t value,
                            TransitionExecutionInput input,
                            std::span<uint64_t> scratch)
        {
            switch(location.area())
            {
                case TransitionLocationArea::RegisterFile:
                    fatal("transition program writes its register file");
                case TransitionLocationArea::Stack:
                    store_stack_word(input.frame_pointer, location.offset(),
                                     value);
                    return;
                case TransitionLocationArea::Scratch:
                    {
                        size_t offset = static_cast<size_t>(location.offset());
                        assert(offset < scratch.size());
                        scratch[offset] = value;
                        return;
                    }
            }
            fatal("invalid transition location area");
        }
    }  // namespace

    InterpreterResumeState execute_transition_program(
        TransitionExecutionContext &context,
        std::span<const TransitionInstruction> instructions,
        TransitionExecutionInput input)
    {
        assert(!instructions.empty());
        assert(instructions.front().kind() ==
               TransitionInstructionKind::BeginTransition);
        assert(input.frame_pointer != nullptr);

        std::span<uint64_t> scratch =
            context.ensure_scratch(instructions.front().scratch_slot_count());
        for(size_t index = 1; index < instructions.size(); ++index)
        {
            const TransitionInstruction &instruction = instructions[index];
            switch(instruction.kind())
            {
                case TransitionInstructionKind::BeginTransition:
                    fatal("transition executor encountered a second header");
                case TransitionInstructionKind::Transfer:
                    {
                        uint64_t value = read_location(
                            instruction.transfer_source(), input, scratch);
                        write_location(instruction.transfer_destination(),
                                       value, input, scratch);
                        break;
                    }
                case TransitionInstructionKind::ResumeInterpreter:
                    return {
                        value_from_word(
                            read_location(instruction.interpreter_accumulator(),
                                          input, scratch)),
                        thread_state_from_word(read_location(
                            instruction.interpreter_thread_state(), input,
                            scratch)),
                        instruction.resume_pc(),
                    };
                default:
                    fatal("unsupported transition instruction");
            }
        }
        fatal("transition executor reached the end without a terminal");
    }

}  // namespace cl::jit
