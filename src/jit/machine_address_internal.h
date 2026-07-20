#ifndef CL_JIT_MACHINE_ADDRESS_INTERNAL_H
#define CL_JIT_MACHINE_ADDRESS_INTERNAL_H

#include "jit/machine_address.h"

#include <cassert>
#include <cstdint>

namespace cl::jit::detail
{
    // Construction is confined to code-cache and platform-memory code. Target
    // encoders receive MachineAddress values and cannot derive them from their
    // writable instruction pointers.
    class MachineAddressAccess
    {
    public:
        static MachineAddress from_pointer(const void *pointer)
        {
            assert(pointer != nullptr);
            return MachineAddress(reinterpret_cast<uintptr_t>(pointer));
        }

        static MachineAddress from_bits(uintptr_t bits)
        {
            return MachineAddress(bits);
        }
    };

}  // namespace cl::jit::detail

#endif  // CL_JIT_MACHINE_ADDRESS_INTERNAL_H
