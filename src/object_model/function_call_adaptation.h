#ifndef CL_FUNCTION_CALL_ADAPTATION_H
#define CL_FUNCTION_CALL_ADAPTATION_H

#include <cstdint>

namespace cl
{
    enum class FunctionCallAdaptation : uint8_t
    {
        FixedArity,
        Defaultable,
        Full,
    };
}  // namespace cl

#endif
