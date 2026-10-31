#ifndef CL_HEAP_SPACE_H
#define CL_HEAP_SPACE_H

#include <cstddef>
#include <memory>

namespace cl
{
    class SlabAllocator;

    // Owns slab-backed storage. Returned addresses are borrowed until space
    // destruction; reservations are not constructed or registered heap objects.
    class HeapSpace
    {
    public:
        HeapSpace() noexcept;
        ~HeapSpace();

        HeapSpace(const HeapSpace &) = delete;
        HeapSpace &operator=(const HeapSpace &) = delete;
        HeapSpace(HeapSpace &&) = delete;
        HeapSpace &operator=(HeapSpace &&) = delete;

        // One positive allocated extent, not a copy extent. Invalid sizing or
        // allocation failure is fatal. This does not construct a HeapObject.
        [[nodiscard]] std::byte *allocate(size_t allocated_bytes) noexcept;

    private:
        struct SlabListNode;
        SlabAllocator &add_slab(size_t allocation_size) noexcept;

        // The owning list head is also the current allocation slab.
        std::unique_ptr<SlabListNode> slabs_;
    };
}  // namespace cl

#endif  // CL_HEAP_SPACE_H
