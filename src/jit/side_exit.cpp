#include "jit/side_exit.h"

#include "jit/compilation_storage.h"
#include "runtime/fatal.h"

#include <absl/container/flat_hash_set.h>

#include <vector>

namespace cl::jit
{
    SideExit::SideExit(const CompilationStorage &storage,
                       std::span<const ProgramValueRef> inputs,
                       std::span<const InstructionId> instructions)
        : inputs_(inputs.begin(), inputs.end()),
          instructions_(instructions.begin(), instructions.end())
    {
        if(instructions_.empty())
        {
            fatal("JIT side exit has no retained instructions");
        }

        absl::flat_hash_set<InstructionId> retained;
        for(InstructionId id: instructions_)
        {
            Instruction instruction = storage.instruction(id);
            if(instruction.is_detached())
            {
                fatal("JIT side exit contains a detached instruction");
            }
            if(!retained.insert(id).second)
            {
                fatal("JIT side exit contains one instruction more than once");
            }
        }
        if(storage.instruction(instructions_.back()).kind() !=
           InstructionKind::Snapshot)
        {
            fatal("JIT side exit does not end in a Snapshot");
        }

        absl::flat_hash_set<InstructionId> available;
        absl::flat_hash_set<InstructionId> input_set;
        std::vector<InstructionId> expected_inputs;
        for(size_t index = 0; index < instructions_.size(); ++index)
        {
            Instruction instruction = storage.instruction(instructions_[index]);
            if(index + 1 != instructions_.size() &&
               instruction.kind() == InstructionKind::Snapshot)
            {
                fatal("JIT side exit contains a Snapshot before its final "
                      "instruction");
            }
            visit_operand_references(
                instruction,
                [&](uint32_t, OperandClass operand_class,
                    ValueRepresentationRequirement, InstructionId definition) {
                    if(operand_class != OperandClass::ProgramValue)
                    {
                        fatal("JIT side exit contains a non-program-value "
                              "dependency");
                    }
                    if(available.contains(definition))
                    {
                        return;
                    }
                    if(retained.contains(definition))
                    {
                        fatal("JIT side exit references a retained instruction "
                              "before its definition");
                    }
                    if(input_set.insert(definition).second)
                    {
                        expected_inputs.push_back(definition);
                    }
                });
            available.insert(instruction.id());
        }

        if(inputs_.size() != expected_inputs.size())
        {
            fatal("JIT side exit inputs do not match its external operand "
                  "environment");
        }
        for(size_t index = 0; index < inputs_.size(); ++index)
        {
            if(inputs_[index].instruction_id() != expected_inputs[index])
            {
                fatal("JIT side exit inputs do not match its external operand "
                      "environment");
            }
        }
    }

}  // namespace cl::jit
