#ifndef CL_JIT_STANDARD_CODE_MEMORY_H
#define CL_JIT_STANDARD_CODE_MEMORY_H

#include "jit/platform_code_memory.h"

#include <cstddef>
#include <memory>

namespace cl::jit
{
    class StandardCodeMemory final : public PlatformCodeMemory
    {
    public:
        StandardCodeMemory();

        size_t page_size() const override { return page_size_; }
        size_t code_allocation_granularity() const override
        {
            return page_size_;
        }

        [[nodiscard]] Result<std::unique_ptr<PlatformCodeSlab>, CodeCacheError>
        allocate_slab(size_t size) override;

    private:
        size_t page_size_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_STANDARD_CODE_MEMORY_H
