#include "jit/compilation_storage.h"

#include "runtime/fatal.h"

#include <cassert>
#include <limits>

namespace cl::jit
{
    Instruction *CompilationStorage::instruction(InstructionId id)
    {
        assert(id.value() < instructions_.size());
        return instructions_[id.value()].instruction();
    }

    const Instruction *CompilationStorage::instruction(InstructionId id) const
    {
        assert(id.value() < instructions_.size());
        return instructions_[id.value()].instruction();
    }

    InstructionId CompilationStorage::next_instruction_id() const
    {
        if(instructions_.size() > std::numeric_limits<uint32_t>::max())
        {
            fatal("too many JIT instructions");
        }
        return InstructionId(static_cast<uint32_t>(instructions_.size()));
    }

}  // namespace cl::jit
