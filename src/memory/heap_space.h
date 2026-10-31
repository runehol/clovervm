#ifndef CL_HEAP_SPACE_H
#define CL_HEAP_SPACE_H

#include <array>
#include <cstddef>
#include <memory>
#include <span>

namespace cl
{
    class SlabAllocator;

    struct GroupReservation
    {
        std::array<std::byte *, 2> members{};
        size_t count = 0;
    };

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

        // One or two positive allocated extents, not copy extents. The entire
        // group fits in one slab. Invalid sizing/allocation failure is fatal.
        [[nodiscard]] GroupReservation
        reserve_group(std::span<const size_t> allocated_sizes) noexcept;

    private:
        struct Chunk;
        SlabAllocator &grow(size_t group_size) noexcept;

        std::unique_ptr<Chunk> chunks_;
        SlabAllocator *current_ = nullptr;
    };
}  // namespace cl

#endif  // CL_HEAP_SPACE_H
