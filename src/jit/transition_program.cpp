#include "jit/transition_program.h"

#include "runtime/fatal.h"

#include <fmt/format.h>

#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace cl::jit
{
    namespace
    {
        bool has_implicit_output(TransitionInstructionKind kind)
        {
            return instruction_result_class(kind) != ResultClass::None;
        }

        void require_initialized_scratch(TransitionLocation location,
                                         const std::vector<bool> &initialized)
        {
            if(location.area() != TransitionLocationArea::Scratch)
            {
                return;
            }
            size_t index = static_cast<size_t>(location.offset());
            if(index >= initialized.size() || !initialized[index])
            {
                fatal("transition program reads uninitialized scratch");
            }
        }

        void require_declared_scratch(TransitionLocation location,
                                      uint32_t scratch_slot_count)
        {
            if(location.area() == TransitionLocationArea::Scratch &&
               static_cast<uint32_t>(location.offset()) >= scratch_slot_count)
            {
                fatal("transition program scratch location exceeds header");
            }
        }

        std::string format_location(TransitionLocation location)
        {
            switch(location.area())
            {
                case TransitionLocationArea::RegisterFile:
                    return fmt::format("register_file[{}]", location.offset());
                case TransitionLocationArea::Stack:
                    return fmt::format("stack[{}]", location.offset());
                case TransitionLocationArea::Scratch:
                    return fmt::format("scratch[{}]", location.offset());
            }
            fatal("invalid transition location area");
        }
    }  // namespace

    TransitionProgramBuilder::TransitionProgramBuilder()
    {
        instructions_.push_back(
            TransitionInstruction::begin_transition(uint32_t{0}));
    }

    void TransitionProgramBuilder::append_instruction(
        TransitionInstruction instruction)
    {
        size_t entry_index = instructions_.size();
        if(has_implicit_output(instruction.kind()))
        {
            if(entry_index >
               static_cast<size_t>(std::numeric_limits<int16_t>::max()))
            {
                fatal("transition program has too many result instructions");
            }
            require_scratch_slot(static_cast<uint32_t>(entry_index));
        }
        if(instruction.kind() == TransitionInstructionKind::Transfer)
        {
            TransitionLocation destination = instruction.transfer_destination();
            if(destination.area() == TransitionLocationArea::Scratch)
            {
                require_scratch_slot(
                    static_cast<uint32_t>(destination.offset()));
            }
        }
        instructions_.push_back(instruction);
    }

    void
    TransitionProgramBuilder::emplace_transfer(TransitionLocation destination,
                                               TransitionLocation source)
    {
        append_instruction(
            TransitionInstruction::transfer(destination, source));
    }

    void TransitionProgramBuilder::emplace_resume_interpreter(
        TransitionLocation accumulator, BytecodePC resume_pc)
    {
        append_instruction(
            TransitionInstruction::resume_interpreter(accumulator, resume_pc));
    }

    std::vector<TransitionInstruction> TransitionProgramBuilder::finalize() &&
    {
        verify_transition_program(instructions_);
        return std::move(instructions_);
    }

    void TransitionProgramBuilder::require_scratch_slot(uint32_t slot)
    {
        TransitionInstruction &header = instructions_.front();
        uint32_t required = slot + 1;
        if(header.scratch_slot_count() < required)
        {
            header.set_scratch_slot_count(required);
        }
    }

    void verify_transition_program(
        std::span<const TransitionInstruction> instructions)
    {
        if(instructions.empty() ||
           instructions.front().kind() !=
               TransitionInstructionKind::BeginTransition)
        {
            fatal("transition program does not begin with BeginTransition");
        }
        if(instructions.size() == 1)
        {
            fatal("transition program has no terminal instruction");
        }

        uint32_t scratch_slot_count = instructions.front().scratch_slot_count();
        std::vector<bool> initialized_scratch(scratch_slot_count, false);
        for(size_t index = 1; index < instructions.size(); ++index)
        {
            const TransitionInstruction &instruction = instructions[index];
            switch(instruction.kind())
            {
                case TransitionInstructionKind::BeginTransition:
                    fatal("transition program contains a second "
                          "BeginTransition");
                case TransitionInstructionKind::Transfer:
                    {
                        TransitionLocation source =
                            instruction.transfer_source();
                        TransitionLocation destination =
                            instruction.transfer_destination();
                        require_initialized_scratch(source,
                                                    initialized_scratch);
                        require_declared_scratch(source, scratch_slot_count);
                        require_declared_scratch(destination,
                                                 scratch_slot_count);
                        if(destination.area() ==
                           TransitionLocationArea::RegisterFile)
                        {
                            fatal("transition program writes its register "
                                  "file");
                        }
                        if(destination.area() ==
                           TransitionLocationArea::Scratch)
                        {
                            initialized_scratch[static_cast<size_t>(
                                destination.offset())] = true;
                        }
                        break;
                    }
                case TransitionInstructionKind::ResumeInterpreter:
                    require_initialized_scratch(
                        instruction.interpreter_accumulator(),
                        initialized_scratch);
                    require_declared_scratch(
                        instruction.interpreter_accumulator(),
                        scratch_slot_count);
                    if(index + 1 != instructions.size())
                    {
                        fatal("transition terminal is not the final "
                              "instruction");
                    }
                    break;
                default:
                    if(!has_implicit_output(instruction.kind()))
                    {
                        fatal("unsupported transition instruction");
                    }
                    if(index >= scratch_slot_count)
                    {
                        fatal("transition result exceeds scratch header");
                    }
                    initialized_scratch[index] = true;
                    break;
            }
        }

        if(instructions.back().kind() !=
           TransitionInstructionKind::ResumeInterpreter)
        {
            fatal("transition program has no final terminal instruction");
        }
    }

    std::string format_transition_program(
        std::span<const TransitionInstruction> instructions)
    {
        std::string result = "transition {\n";
        for(size_t index = 0; index < instructions.size(); ++index)
        {
            const TransitionInstruction &instruction = instructions[index];
            if(index == 0)
            {
                if(instruction.kind() !=
                   TransitionInstructionKind::BeginTransition)
                {
                    fatal("transition dump has no BeginTransition header");
                }
                fmt::format_to(std::back_inserter(result),
                               "  0: begin_transition "
                               "{{scratch_slots = {}}}\n",
                               instruction.scratch_slot_count());
                continue;
            }

            switch(instruction.kind())
            {
                case TransitionInstructionKind::BeginTransition:
                    fatal("transition dump contains a second BeginTransition");
                case TransitionInstructionKind::Transfer:
                    fmt::format_to(
                        std::back_inserter(result), "  {}: transfer {}, {}\n",
                        index,
                        format_location(instruction.transfer_destination()),
                        format_location(instruction.transfer_source()));
                    break;
                case TransitionInstructionKind::ResumeInterpreter:
                    fmt::format_to(
                        std::back_inserter(result),
                        "  {}: resume_interpreter {} {{resume_pc = {}}}\n",
                        index,
                        format_location(instruction.interpreter_accumulator()),
                        instruction.resume_pc());
                    break;
                default:
                    fmt::format_to(std::back_inserter(result),
                                   "  {}: instruction {}\n", index,
                                   static_cast<uint16_t>(instruction.kind()));
                    break;
            }
        }
        result += "}\n";
        return result;
    }

}  // namespace cl::jit
