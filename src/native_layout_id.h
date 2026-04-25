#ifndef CL_NATIVE_LAYOUT_ID_H
#define CL_NATIVE_LAYOUT_ID_H

#include <cstdint>

namespace cl
{
    enum class NativeLayoutId : uint8_t
    {
        Invalid = 0,

        String,
        List,
        Dict,
        Function,
        BuiltinFunction,
        RangeIterator,
        CodeObject,
        ClassObject,
        Instance,

        Count,
    };

}  // namespace cl

#endif  // CL_NATIVE_LAYOUT_ID_H
