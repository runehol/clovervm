#ifndef CL_JIT_MAP_JIT_CODE_MEMORY_H
#define CL_JIT_MAP_JIT_CODE_MEMORY_H

#include "jit/platform_code_memory.h"

#include <cstddef>
#include <memory>

namespace cl::jit
{
    class MapJitCodeMemory final : public PlatformCodeMemory
    {
    public:
        MapJitCodeMemory();

        size_t page_size() const override { return page_size_; }
        size_t code_allocation_granularity() const override { return 16; }

        [[nodiscard]] Result<std::unique_ptr<PlatformCodeSlab>, JitCodeError>
        allocate_slab(size_t size) override;

    private:
        size_t page_size_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_MAP_JIT_CODE_MEMORY_H
