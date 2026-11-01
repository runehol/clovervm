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

    struct HeapSpace::SlabListNode
    {
        explicit SlabListNode(size_t mapping_size)
            : slab(tag_offset, mapping_size)
        {
        }

        SlabAllocator slab;
        std::unique_ptr<SlabListNode> next;
    };

    HeapSpace::HeapSpace() noexcept = default;

    HeapSpace::~HeapSpace()
    {
        // Avoid recursively destroying an arbitrarily long ownership chain.
        while(slabs_)
        {
            auto next = std::move(slabs_->next);
            slabs_ = std::move(next);
        }
    }

    void HeapSpace::visit_memory_ranges(
        absl::FunctionRef<void(uintptr_t begin, uintptr_t end)> visitor) const
    {
        for(const SlabListNode *node = slabs_.get(); node != nullptr;
            node = node->next.get())
        {
            visitor(reinterpret_cast<uintptr_t>(node->slab.start()),
                    reinterpret_cast<uintptr_t>(node->slab.end()));
        }
    }

    SlabAllocator &HeapSpace::add_slab(size_t allocation_size) noexcept
    {
        size_t mapping_size =
            std::max(DefaultSlabSize,
                     checked_round_up(checked_add(tag_offset, allocation_size),
                                      SlabLookupGranuleSize));
        if(mapping_size >
           static_cast<size_t>(std::numeric_limits<ptrdiff_t>::max()))
        {
            fatal("collector reservation mapping is too large");
        }

        // One native bookkeeping allocation per slab, never per reservation.
        // No exception handler is needed in the runtime's no-exceptions build.
        std::unique_ptr<SlabListNode> node(new(std::nothrow)
                                               SlabListNode(mapping_size));
        if(!node)
        {
            fatal("failed to allocate collector reservation bookkeeping");
        }
        node->next = std::move(slabs_);
        slabs_ = std::move(node);
        return slabs_->slab;
    }

    std::byte *HeapSpace::allocate(size_t allocated_bytes) noexcept
    {
        if(allocated_bytes == 0)
        {
            fatal("collector reservation extent must be positive");
        }
        size_t allocation_size = checked_round_up(allocated_bytes, stride);

        char *base = slabs_ ? slabs_->slab.allocate(allocation_size) : nullptr;
        if(base == nullptr)
        {
            base = add_slab(allocation_size).allocate(allocation_size);
            if(base == nullptr)
            {
                fatal("collector reservation did not fit its new slab");
            }
        }

        return reinterpret_cast<std::byte *>(base);
    }
}  // namespace cl
