#ifndef CL_JIT_TRANSITION_PROGRAM_H
#define CL_JIT_TRANSITION_PROGRAM_H

#include "jit/instruction.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace cl::jit
{
    enum class TransitionLocationArea : uint8_t
    {
        RegisterFile,
        Stack,
        Scratch,
    };

    class TransitionLocation
    {
    public:
        static TransitionLocation register_file(int16_t index)
        {
            assert(index >= 0);
            return TransitionLocation(TransitionLocationArea::RegisterFile,
                                      index);
        }

        static TransitionLocation stack(int16_t frame_offset)
        {
            return TransitionLocation(TransitionLocationArea::Stack,
                                      frame_offset);
        }

        static TransitionLocation scratch(int16_t index)
        {
            assert(index >= 0);
            return TransitionLocation(TransitionLocationArea::Scratch, index);
        }

        TransitionLocationArea area() const { return area_; }
        int16_t offset() const { return offset_; }

        friend bool operator==(TransitionLocation,
                               TransitionLocation) = default;

        template <typename H>
        friend H AbslHashValue(H hash, TransitionLocation location)
        {
            return H::combine(std::move(hash), location.area_,
                              location.offset_);
        }

    private:
        TransitionLocation(TransitionLocationArea area, int16_t offset)
            : area_(area), offset_(offset)
        {
        }

        TransitionLocationArea area_;
        int16_t offset_;
    };

    static_assert(sizeof(TransitionLocation) == sizeof(uint32_t));

    class alignas(8) TransitionInstruction
    {
    public:
        static TransitionInstruction
        begin_transition(uint32_t scratch_slot_count)
        {
            TransitionInstruction result(
                TransitionInstructionKind::BeginTransition);
            result.slots_[0] = scratch_slot_count;
            return result;
        }

        static TransitionInstruction transfer(TransitionLocation destination,
                                              TransitionLocation source)
        {
            TransitionInstruction result(TransitionInstructionKind::Transfer);
            result.set_location(0, destination);
            result.set_location(1, source);
            return result;
        }

        static TransitionInstruction
        resume_interpreter(TransitionLocation accumulator,
                           TransitionLocation thread_state,
                           BytecodePC resume_pc)
        {
            TransitionInstruction result(
                TransitionInstructionKind::ResumeInterpreter);
            result.set_location(0, accumulator);
            result.set_location(1, thread_state);
            result.slots_[2] = resume_pc;
            return result;
        }

        TransitionInstructionKind kind() const { return kind_; }

        uint32_t scratch_slot_count() const
        {
            assert(kind_ == TransitionInstructionKind::BeginTransition);
            return slots_[0];
        }

        void set_scratch_slot_count(uint32_t scratch_slot_count)
        {
            assert(kind_ == TransitionInstructionKind::BeginTransition);
            slots_[0] = scratch_slot_count;
        }

        TransitionLocation transfer_source() const
        {
            assert(kind_ == TransitionInstructionKind::Transfer);
            return location(1);
        }

        TransitionLocation transfer_destination() const
        {
            assert(kind_ == TransitionInstructionKind::Transfer);
            return location(0);
        }

        TransitionLocation interpreter_accumulator() const
        {
            assert(kind_ == TransitionInstructionKind::ResumeInterpreter);
            return location(0);
        }

        TransitionLocation interpreter_thread_state() const
        {
            assert(kind_ == TransitionInstructionKind::ResumeInterpreter);
            return location(1);
        }

        BytecodePC resume_pc() const
        {
            assert(kind_ == TransitionInstructionKind::ResumeInterpreter);
            return slots_[2];
        }

    private:
        explicit TransitionInstruction(TransitionInstructionKind kind)
            : slots_{}, kind_(kind), reserved_(0)
        {
        }

        void set_location(size_t slot, TransitionLocation location)
        {
            assert(slot < 3);
            static_assert(sizeof(location) == sizeof(slots_[slot]));
            std::memcpy(&slots_[slot], &location, sizeof(location));
        }

        TransitionLocation location(size_t slot) const
        {
            assert(slot < 3);
            TransitionLocation result = TransitionLocation::register_file(0);
            static_assert(sizeof(result) == sizeof(slots_[slot]));
            std::memcpy(&result, &slots_[slot], sizeof(result));
            return result;
        }

        uint32_t slots_[3];
        TransitionInstructionKind kind_;
        [[maybe_unused]] uint16_t reserved_;
    };

    static_assert(sizeof(TransitionInstruction) == 16);
    static_assert(alignof(TransitionInstruction) == 8);

    class TransitionProgramBuilder
    {
    public:
        TransitionProgramBuilder();

        uint32_t next_scratch_slot() const
        {
            return instructions_.front().scratch_slot_count();
        }

        void append_instruction(TransitionInstruction instruction);

        void emplace_transfer(TransitionLocation destination,
                              TransitionLocation source);
        void emplace_resume_interpreter(TransitionLocation accumulator,
                                        TransitionLocation thread_state,
                                        BytecodePC resume_pc);

        std::vector<TransitionInstruction> finalize() &&;

    private:
        void require_scratch_slot(uint32_t slot);

        std::vector<TransitionInstruction> instructions_;
    };

    void verify_transition_program(
        std::span<const TransitionInstruction> instructions);

    std::string format_transition_program(
        std::span<const TransitionInstruction> instructions);

}  // namespace cl::jit

#endif  // CL_JIT_TRANSITION_PROGRAM_H
