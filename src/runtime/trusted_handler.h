#ifndef CL_TRUSTED_HANDLER_H
#define CL_TRUSTED_HANDLER_H

#include "object_model/value.h"

#include <cstdint>

namespace cl
{
    class ThreadState;

    using UnaryHandler = Value (*)(ThreadState *, Value);
    using BinaryHandler = Value (*)(ThreadState *, Value, Value);
    using TernaryHandler = Value (*)(ThreadState *, Value, Value, Value);
    using TrustedHandlerTarget = void (*)();

    enum class TrustedHandlerArity : uint8_t
    {
        None,
        Unary,
        Binary,
        Ternary,
    };

}  // namespace cl

#endif  // CL_TRUSTED_HANDLER_H
