#ifndef CL_JIT_CODE_CACHE_TYPES_H
#define CL_JIT_CODE_CACHE_TYPES_H

#include "jit/machine_address.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace cl::jit
{
    enum class JitCodeError : uint8_t
    {
        PoolOutOfRange,
        AllocationFailure,
        PublicationFailure,
    };

    class CodeSlice
    {
    public:
        CodeSlice(MachineAddress execute_address, size_t capacity)
            : execute_address_(execute_address), capacity_(capacity)
        {
            assert(execute_address.offset_within(4) == 0);
        }

        MachineAddress execute_address() const { return execute_address_; }
        size_t capacity() const { return capacity_; }

    private:
        MachineAddress execute_address_;
        size_t capacity_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_CODE_CACHE_TYPES_H
