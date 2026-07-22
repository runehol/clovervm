#include "jit/standard_code_memory.h"

#include "jit/machine_address_internal.h"

#include <bit>
#include <cassert>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>

namespace cl::jit
{
    namespace
    {
        class StandardCodeSlab final : public PlatformCodeSlab
        {
        public:
            StandardCodeSlab(void *base, size_t size, size_t page_size)
                : base_(static_cast<uint8_t *>(base)), size_(size)
#ifndef NDEBUG
                  ,
                  page_size_(page_size)
#endif
            {
                assert(base != nullptr);
                assert(reinterpret_cast<uintptr_t>(base) % page_size == 0);
                assert(size != 0);
                assert(size % page_size == 0);
            }

            ~StandardCodeSlab() override
            {
                int result = munmap(base_, size_);
                assert(result == 0);
                (void)result;
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

            Result<void, JitCodeError> commit(size_t, size_t, size_t,
                                              size_t) override
            {
                return Result<void, JitCodeError>::ok();
            }

            void begin_code_write() override {}
            void end_code_write() override {}

            Result<void, JitCodeError> publish(size_t offset,
                                               size_t encoded_size,
                                               size_t protected_size) override
            {
                assert(offset % page_size_ == 0);
                assert(protected_size != 0);
                assert(protected_size % page_size_ == 0);
                assert(encoded_size <= protected_size);
                assert(offset <= size_);
                assert(protected_size <= size_ - offset);

                char *begin = reinterpret_cast<char *>(base_ + offset);
                __builtin___clear_cache(begin, begin + encoded_size);
                if(mprotect(base_ + offset, protected_size,
                            PROT_READ | PROT_EXEC) != 0)
                {
                    return Result<void, JitCodeError>::error(
                        JitCodeError::PublicationFailure);
                }
                return Result<void, JitCodeError>::ok();
            }

        private:
            uint8_t *base_;
            size_t size_;
#ifndef NDEBUG
            size_t page_size_;
#endif
        };
    }  // namespace

    StandardCodeMemory::StandardCodeMemory()
    {
        long result = sysconf(_SC_PAGESIZE);
        assert(result > 0);
        page_size_ = static_cast<size_t>(result);
        assert(std::has_single_bit(page_size_));
        assert(page_size_ >= 16);
    }

    Result<std::unique_ptr<PlatformCodeSlab>, JitCodeError>
    StandardCodeMemory::allocate_slab(size_t size)
    {
        assert(size != 0);
        assert(size % page_size_ == 0);
        void *base = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
        if(base == MAP_FAILED)
        {
            return Result<std::unique_ptr<PlatformCodeSlab>,
                          JitCodeError>::error(JitCodeError::AllocationFailure);
        }
        return Result<std::unique_ptr<PlatformCodeSlab>, JitCodeError>::ok(
            std::make_unique<StandardCodeSlab>(base, size, page_size_));
    }

}  // namespace cl::jit
