#ifndef CL_SLAB_ALLOCATOR_H
#define CL_SLAB_ALLOCATOR_H

#include "heap_constants.h"
#include "value.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

namespace cl
{
    class GlobalHeap;

    class SlabAllocator
    {
    public:
        static constexpr size_t ValidObjectBitmapGranule = 32;
        static constexpr size_t ValidObjectBitmapBits =
            DefaultSlabSize / ValidObjectBitmapGranule;
        static constexpr size_t ValidObjectBitmapWords =
            (ValidObjectBitmapBits + 63) / 64;

        SlabAllocator(GlobalHeap *global_heap, size_t offset, size_t slab_size);
        ~SlabAllocator();

        void add_reclaim_blocker() { ++n_reclaim_blockers; }
        void drop_reclaim_blocker();
        uint32_t reclaim_blocker_count() const { return n_reclaim_blockers; }

        char *start() const { return start_ptr; }
        char *end() const { return end_ptr; }
        char *first_valid_object_slot_for_testing() const
        {
            return first_object_header;
        }

        void mark_valid_object(HeapObject *obj);
        void clear_valid_object(HeapObject *obj)
        {
            size_t bit_idx = valid_object_bit_index(obj);
            uint64_t mask = uint64_t(1) << (bit_idx % 64);
            uint64_t &word = valid_object_bitmap[bit_idx / 64];
            assert((word & mask) != 0);
            word &= ~mask;
        }

        bool has_valid_objects() const
        {
            for(uint64_t word: valid_object_bitmap)
            {
                if(word != 0)
                {
                    return true;
                }
            }
            return false;
        }

        template <typename Fn> void for_each_valid_object(Fn &&fn) const
        {
            static_assert(std::is_invocable_v<Fn, HeapObject *>);
            if(first_object_header == nullptr)
            {
                assert(!has_valid_objects());
                return;
            }

            for(size_t word_idx = 0; word_idx < valid_object_bitmap.size();
                ++word_idx)
            {
                uint64_t word = valid_object_bitmap[word_idx];
                while(word != 0)
                {
                    unsigned bit_in_word = __builtin_ctzll(word);
                    size_t bit_idx = word_idx * 64 + bit_in_word;
                    assert(bit_idx < ValidObjectBitmapBits);
                    char *header = first_object_header +
                                   bit_idx * ValidObjectBitmapGranule;
                    assert(header >= start_ptr);
                    assert(header < end_ptr);
                    fn(reinterpret_cast<HeapObject *>(header));
                    word &= word - 1;
                }
            }
        }

        char *allocate(size_t n_bytes)
        {
            if(curr_ptr + n_bytes > end_ptr)
            {
                return nullptr;
            }
            char *result = curr_ptr;
            n_bytes = (n_bytes + value_ptr_granularity - 1) &
                      ~(value_ptr_granularity - 1);
            curr_ptr += n_bytes;
            return result;
        }

    private:
        size_t valid_object_bit_index(HeapObject *obj) const
        {
            assert(obj != nullptr);
            assert(first_object_header != nullptr);
            char *object_header = reinterpret_cast<char *>(obj);
            assert(object_header >= first_object_header);
            assert(object_header < end_ptr);
            uintptr_t delta =
                static_cast<uintptr_t>(object_header - first_object_header);
            assert(delta % ValidObjectBitmapGranule == 0);
            size_t bit_idx = delta / ValidObjectBitmapGranule;
            assert(bit_idx < ValidObjectBitmapBits);
            return bit_idx;
        }

        GlobalHeap *global_heap;
        char *start_ptr;
        char *curr_ptr;
        char *end_ptr;
        char *first_object_header;
        std::array<uint64_t, ValidObjectBitmapWords> valid_object_bitmap = {};
        uint32_t n_reclaim_blockers = 0;
    };

    inline void SlabAllocator::mark_valid_object(HeapObject *obj)
    {
        assert(obj != nullptr);
        assert(reinterpret_cast<char *>(obj) >= start_ptr);
        assert(reinterpret_cast<char *>(obj) < end_ptr);

        size_t bit_idx = valid_object_bit_index(obj);
        uint64_t mask = uint64_t(1) << (bit_idx % 64);
        uint64_t &word = valid_object_bitmap[bit_idx / 64];
        assert((word & mask) == 0);
        word |= mask;
    }

}  // namespace cl

#endif  // CL_SLAB_ALLOCATOR_H
