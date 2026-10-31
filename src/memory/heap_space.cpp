#include "memory/heap_space.h"

#include "memory/heap_constants.h"
#include "memory/slab_allocator.h"
#include "object_model/value_tags.h"
#include "runtime/fatal.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace cl
{
    namespace
    {
        constexpr size_t stride = value_ptr_granularity;
        constexpr size_t tag_offset = value_refcounted_ptr_tag;
        static_assert((stride & (stride - 1)) == 0);
        static_assert((SlabLookupGranuleSize & (SlabLookupGranuleSize - 1)) ==
                      0);

        size_t checked_add(size_t left, size_t right)
        {
            size_t result;
            if(__builtin_add_overflow(left, right, &result))
            {
                fatal("collector reservation size overflow");
            }
            return result;
        }

        size_t checked_round_up(size_t size, size_t alignment)
        {
            return checked_add(size, alignment - 1) & ~(alignment - 1);
        }
    }  // namespace

    struct HeapSpace::Chunk
    {
        explicit Chunk(size_t mapping_size) : slab(tag_offset, mapping_size) {}

        SlabAllocator slab;
        std::unique_ptr<Chunk> next;
    };

    HeapSpace::HeapSpace() noexcept = default;

    HeapSpace::~HeapSpace()
    {
        // Avoid recursively destroying an arbitrarily long ownership chain.
        while(chunks_)
        {
            auto next = std::move(chunks_->next);
            chunks_ = std::move(next);
        }
    }

    SlabAllocator &HeapSpace::grow(size_t group_size) noexcept
    {
        size_t mapping_size =
            std::max(DefaultSlabSize,
                     checked_round_up(checked_add(tag_offset, group_size),
                                      SlabLookupGranuleSize));
        if(mapping_size >
           static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max()))
        {
            fatal("collector reservation mapping is too large");
        }

        // One native bookkeeping allocation per slab, never per reservation.
        // No exception handler is needed in the runtime's no-exceptions build.
        std::unique_ptr<Chunk> chunk(new(std::nothrow) Chunk(mapping_size));
        if(!chunk)
        {
            fatal("failed to allocate collector reservation bookkeeping");
        }
        chunk->next = std::move(chunks_);
        chunks_ = std::move(chunk);
        current_ = &chunks_->slab;
        return *current_;
    }

    GroupReservation
    HeapSpace::reserve_group(std::span<const size_t> allocated_sizes) noexcept
    {
        if(allocated_sizes.empty() || allocated_sizes.size() > 2)
        {
            fatal("collector reservation requires one or two members");
        }

        std::array<size_t, 2> offsets{};
        size_t group_size = 0;
        for(size_t index = 0; index < allocated_sizes.size(); ++index)
        {
            if(allocated_sizes[index] == 0)
            {
                fatal("collector reservation extent must be positive");
            }
            offsets[index] = group_size;
            group_size = checked_add(
                group_size, checked_round_up(allocated_sizes[index], stride));
        }

        char *base = current_ ? current_->allocate(group_size) : nullptr;
        if(base == nullptr)
        {
            base = grow(group_size).allocate(group_size);
            if(base == nullptr)
            {
                fatal("collector reservation did not fit its new slab");
            }
        }

        GroupReservation result;
        result.count = allocated_sizes.size();
        for(size_t index = 0; index < result.count; ++index)
        {
            result.members[index] =
                reinterpret_cast<std::byte *>(base + offsets[index]);
        }
        return result;
    }
}  // namespace cl
