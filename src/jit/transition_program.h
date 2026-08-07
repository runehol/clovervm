#ifndef CL_JIT_TRANSITION_PROGRAM_H
#define CL_JIT_TRANSITION_PROGRAM_H

#include "jit/instruction.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cl
{
    class CodeObject;
}

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

        TransitionLocationArea area() const
        {
            return static_cast<TransitionLocationArea>(encoded_ & 0xffu);
        }

        int16_t offset() const { return static_cast<int16_t>(encoded_ >> 16); }

        friend bool operator==(TransitionLocation,
                               TransitionLocation) = default;

        template <typename H>
        friend H AbslHashValue(H hash, TransitionLocation location)
        {
            return H::combine(std::move(hash), location.encoded_);
        }

    private:
        TransitionLocation(TransitionLocationArea area, int16_t offset)
            : encoded_(
                  static_cast<uint32_t>(area) |
                  (static_cast<uint32_t>(static_cast<uint16_t>(offset)) << 16))
        {
        }

        explicit TransitionLocation(uint32_t encoded) : encoded_(encoded) {}

        uint32_t encoded() const { return encoded_; }

        friend class TransitionInstruction;

        uint32_t encoded_;
    };

    static_assert(sizeof(TransitionLocation) == sizeof(uint32_t));
    static_assert(std::is_trivially_copyable_v<TransitionLocation>);
    static_assert(std::has_unique_object_representations_v<TransitionLocation>);

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
        resume_interpreter(CodeObject *code_object,
                           BytecodePCOffset resume_pc_offset)
        {
            assert(code_object != nullptr);
            TransitionInstruction result(
                TransitionInstructionKind::ResumeInterpreter);
            static_assert(sizeof(code_object) == sizeof(uint64_t));
            std::memcpy(&result.slots_[0], &code_object, sizeof(code_object));
            result.slots_[2] = resume_pc_offset;
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

        CodeObject *interpreter_code_object() const
        {
            assert(kind_ == TransitionInstructionKind::ResumeInterpreter);
            CodeObject *result = nullptr;
            static_assert(sizeof(result) == sizeof(uint64_t));
            std::memcpy(&result, &slots_[0], sizeof(result));
            return result;
        }

        BytecodePCOffset resume_pc_offset() const
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
            slots_[slot] = location.encoded();
        }

        TransitionLocation location(size_t slot) const
        {
            assert(slot < 3);
            return TransitionLocation(slots_[slot]);
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
        void emplace_resume_interpreter(CodeObject *code_object,
                                        BytecodePCOffset resume_pc_offset);

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
