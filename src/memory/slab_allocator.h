#ifndef CL_SLAB_ALLOCATOR_H
#define CL_SLAB_ALLOCATOR_H

#include "memory/heap_constants.h"
#include "object_model/value.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

namespace cl
{
    class SlabAllocator
    {
    public:
        static constexpr size_t ValidObjectBitmapGranule = 32;
        static constexpr size_t ValidObjectBitmapBits =
            DefaultSlabSize / ValidObjectBitmapGranule;
        static constexpr size_t ValidObjectBitmapWords =
            (ValidObjectBitmapBits + 63) / 64;

        SlabAllocator(size_t offset, size_t slab_size);
        ~SlabAllocator();

        size_t size() const { return slab_size; }
        void reset();

        void add_active_allocator_pin() { ++n_slab_pins; }
        void drop_active_allocator_pin()
        {
            assert(n_slab_pins > 0);
            --n_slab_pins;
        }
        void add_epoch_discovery_pin() { ++n_slab_pins; }
        void drop_epoch_discovery_pin()
        {
            assert(n_slab_pins > 0);
            --n_slab_pins;
        }
        uint32_t slab_pin_count() const { return n_slab_pins; }

        char *start() const { return start_ptr; }
        char *end() const { return start_ptr + slab_size; }
        char *first_object_slot() const { return first_object_header; }

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

        bool has_slab_pins() const { return n_slab_pins != 0; }

        bool has_reclaim_blockers() const
        {
            return has_slab_pins() || has_valid_objects();
        }

        uint64_t count_valid_objects_slow() const
        {
            uint64_t count = 0;
            for(uint64_t word: valid_object_bitmap)
            {
                count += __builtin_popcountll(word);
            }
            return count;
        }

        uint64_t count_reclaim_blockers_slow() const
        {
            return slab_pin_count() + count_valid_objects_slow();
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
                    assert(header < allocation_end_ptr);
                    fn(reinterpret_cast<HeapObject *>(header));
                    word &= word - 1;
                }
            }
        }

        char *allocate(size_t n_bytes)
        {
            size_t remaining =
                static_cast<size_t>(allocation_end_ptr - curr_ptr);
            if(unlikely(n_bytes > remaining))
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
            assert(object_header < allocation_end_ptr);
            uintptr_t delta =
                static_cast<uintptr_t>(object_header - first_object_header);
            assert(delta % ValidObjectBitmapGranule == 0);
            size_t bit_idx = delta / ValidObjectBitmapGranule;
            assert(bit_idx < ValidObjectBitmapBits);
            return bit_idx;
        }

        char *start_ptr;
        char *curr_ptr;
        char *allocation_end_ptr;
        char *first_object_header;
        size_t slab_size;
        std::array<uint64_t, ValidObjectBitmapWords> valid_object_bitmap = {};
        uint32_t n_slab_pins = 0;
    };

    inline void SlabAllocator::mark_valid_object(HeapObject *obj)
    {
        assert(obj != nullptr);
        assert(reinterpret_cast<char *>(obj) >= start_ptr);
        assert(reinterpret_cast<char *>(obj) < allocation_end_ptr);

        size_t bit_idx = valid_object_bit_index(obj);
        uint64_t mask = uint64_t(1) << (bit_idx % 64);
        uint64_t &word = valid_object_bitmap[bit_idx / 64];
        assert((word & mask) == 0);
        word |= mask;
    }

}  // namespace cl

#endif  // CL_SLAB_ALLOCATOR_H
