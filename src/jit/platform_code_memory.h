#ifndef CL_JIT_PLATFORM_CODE_MEMORY_H
#define CL_JIT_PLATFORM_CODE_MEMORY_H

#include "jit/code_cache_types.h"
#include "jit/machine_address.h"
#include "util/result.h"

#include <cstddef>
#include <memory>

namespace cl::jit
{
    class PlatformCodeSlab
    {
    public:
        virtual ~PlatformCodeSlab() = default;

        // Executable code addresses and data addresses at corresponding
        // offsets must occupy the requested logical span. Writable pointers
        // may be unrelated aliases of the same physical storage.
        virtual size_t size() const = 0;
        virtual void *write_pointer_at(size_t offset) const = 0;
        virtual MachineAddress executable_address_at(size_t offset) const = 0;
        virtual MachineAddress data_address_at(size_t offset) const = 0;

        [[nodiscard]] virtual Result<void, CodeCacheError>
        publish(size_t offset, size_t encoded_size, size_t protected_size) = 0;
    };

    class PlatformCodeMemory
    {
    public:
        virtual ~PlatformCodeMemory() = default;

        virtual size_t page_size() const = 0;
        virtual size_t code_allocation_granularity() const = 0;

        [[nodiscard]] virtual Result<std::unique_ptr<PlatformCodeSlab>,
                                     CodeCacheError>
        allocate_slab(size_t size) = 0;
    };

}  // namespace cl::jit

#endif  // CL_JIT_PLATFORM_CODE_MEMORY_H
