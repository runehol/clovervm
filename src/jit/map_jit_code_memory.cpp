#include "jit/map_jit_code_memory.h"

#include "jit/machine_address_internal.h"

#include <cassert>
#include <cstdint>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

namespace cl::jit
{
    namespace
    {
        thread_local size_t jit_write_depth = 0;

        constexpr size_t align_down(size_t value, size_t alignment)
        {
            return value & ~(alignment - 1);
        }

        class MapJitCodeSlab final : public PlatformCodeSlab
        {
        public:
            MapJitCodeSlab(void *base, size_t size, size_t page_size)
                : base_(static_cast<uint8_t *>(base)), size_(size),
                  page_size_(page_size), jit_page_frontier_(size),
                  pool_page_frontier_(size)
            {
                assert(base != nullptr);
                assert(size != 0);
                assert(size % page_size == 0);
            }

            ~MapJitCodeSlab() override
            {
                // A failed pool replacement may have left an unmapped hole.
                // The process will reclaim any surviving mappings if this
                // range cannot be deallocated as one unit.
                (void)munmap(base_, size_);
            }

            size_t size() const override { return size_; }

            void *write_pointer_at(size_t offset) const override
            {
                assert(offset <= size_);
                return base_ + offset;
            }

            MachineAddress executable_address_at(size_t offset) const override
            {
                assert(offset <= size_);
                return detail::MachineAddressAccess::from_pointer(base_ +
                                                                  offset);
            }

            MachineAddress data_address_at(size_t offset) const override
            {
                assert(offset <= size_);
                return detail::MachineAddressAccess::from_pointer(base_ +
                                                                  offset);
            }

            Result<void, JitCodeError> commit(size_t code_offset,
                                              size_t code_size,
                                              size_t pool_offset,
                                              size_t pool_size) override
            {
                if(pool_size == 0)
                {
                    return Result<void, JitCodeError>::ok();
                }

                size_t new_pool_page_frontier =
                    align_down(pool_offset, page_size_);
                assert(code_offset + code_size <= new_pool_page_frontier);
                assert(pool_offset + pool_size <= size_);
                assert(new_pool_page_frontier <= jit_page_frontier_);

                if(new_pool_page_frontier < jit_page_frontier_)
                {
                    uint8_t *begin = base_ + new_pool_page_frontier;
                    size_t size = jit_page_frontier_ - new_pool_page_frontier;
                    if(munmap(begin, size) != 0)
                    {
                        return Result<void, JitCodeError>::error(
                            JitCodeError::AllocationFailure);
                    }

                    // These MAP_JIT pages no longer exist, regardless of
                    // whether installing their RW replacements succeeds.
                    jit_page_frontier_ = new_pool_page_frontier;
                }

                if(jit_page_frontier_ == pool_page_frontier_)
                {
                    return Result<void, JitCodeError>::ok();
                }

                uint8_t *begin = base_ + jit_page_frontier_;
                size_t size = pool_page_frontier_ - jit_page_frontier_;
                void *mapping = mmap(begin, size, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
                if(mapping == MAP_FAILED)
                {
                    return Result<void, JitCodeError>::error(
                        JitCodeError::AllocationFailure);
                }
                assert(mapping == begin);
                pool_page_frontier_ = jit_page_frontier_;
                return Result<void, JitCodeError>::ok();
            }

            void begin_code_write() override
            {
                if(jit_write_depth == 0)
                {
                    pthread_jit_write_protect_np(false);
                }
                ++jit_write_depth;
            }

            void end_code_write() override
            {
                assert(jit_write_depth != 0);
                --jit_write_depth;
                if(jit_write_depth == 0)
                {
                    pthread_jit_write_protect_np(true);
                }
            }

            Result<void, JitCodeError> publish(size_t offset,
                                               size_t encoded_size,
                                               size_t protected_size) override
            {
                assert(offset + protected_size <= jit_page_frontier_);
                char *begin = reinterpret_cast<char *>(base_ + offset);
                __builtin___clear_cache(begin, begin + encoded_size);
                return Result<void, JitCodeError>::ok();
            }

        private:
            uint8_t *base_;
            size_t size_;
            size_t page_size_;
            size_t jit_page_frontier_;
            size_t pool_page_frontier_;
        };
    }  // namespace

    MapJitCodeMemory::MapJitCodeMemory()
    {
        long result = sysconf(_SC_PAGESIZE);
        assert(result > 0);
        page_size_ = static_cast<size_t>(result);
    }

    Result<std::unique_ptr<PlatformCodeSlab>, JitCodeError>
    MapJitCodeMemory::allocate_slab(size_t size)
    {
        assert(size != 0);
        assert(size % page_size_ == 0);
        void *base = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                          MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
        if(base == MAP_FAILED)
        {
            return Result<std::unique_ptr<PlatformCodeSlab>,
                          JitCodeError>::error(JitCodeError::AllocationFailure);
        }
        return Result<std::unique_ptr<PlatformCodeSlab>, JitCodeError>::ok(
            std::make_unique<MapJitCodeSlab>(base, size, page_size_));
    }

}  // namespace cl::jit
