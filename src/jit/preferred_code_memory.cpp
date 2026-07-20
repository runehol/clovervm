#include "jit/platform_code_memory.h"

#include "jit/standard_code_memory.h"

#include <memory>

#if defined(__APPLE__) && defined(__aarch64__)
#include "jit/map_jit_code_memory.h"

#include <pthread.h>
#endif

namespace cl::jit
{
    std::unique_ptr<PlatformCodeMemory> make_preferred_code_memory()
    {
#if defined(__APPLE__) && defined(__aarch64__)
        if(pthread_jit_write_protect_supported_np())
        {
            return std::make_unique<MapJitCodeMemory>();
        }
#endif
        return std::make_unique<StandardCodeMemory>();
    }

}  // namespace cl::jit
