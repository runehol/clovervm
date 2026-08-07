#ifndef CL_JIT_AARCH64_CALL_H
#define CL_JIT_AARCH64_CALL_H

#include "jit/instruction.h"

#include <optional>

namespace cl::jit
{
    struct AArch64CallProperties
    {
        bool permits_call_local_spills;
    };

    constexpr std::optional<AArch64CallProperties>
    aarch64_call_properties(InstructionKind kind)
    {
        switch(kind)
        {
            case InstructionKind::TrustedHandlerCall:
            case InstructionKind::BoxF64:
                return AArch64CallProperties{true};
            default:
                return std::nullopt;
        }
    }
}  // namespace cl::jit

#endif  // CL_JIT_AARCH64_CALL_H
