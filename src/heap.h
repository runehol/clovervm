#ifndef CL_HEAP_H
#define CL_HEAP_H

#include "slab_allocator.h"
#include "typed_value.h"
#include "value.h"
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace cl
{
    static constexpr uintptr_t SlabLookupGranuleShift = 12;
    static constexpr size_t SlabLookupGranuleSize = size_t(1)
                                                    << SlabLookupGranuleShift;

    struct HeapAllocation
    {
        char *memory;
        SlabAllocator *slab;
    };

    uintptr_t slab_lookup_key_for_address(const void *ptr);
    void commit_heap_allocation(const HeapAllocation &allocation,
                                HeapObject *obj);

    template <typename T, typename = void>
    struct HasObjectLayout : std::false_type
    {
    };

    template <typename T>
    struct HasObjectLayout<
        T, std::void_t<decltype(T::has_dynamic_layout),
                       decltype(T::static_value_offset_in_words)>>
        : std::true_type
    {
    };

    template <typename T, typename AllocateFn, typename... Args>
    T *construct_dynamic_object(AllocateFn &&allocate_fn, Args &&...args)
    {
        static_assert(std::is_base_of_v<HeapObject, T>);
        static_assert(HasObjectLayout<T>::value && T::has_dynamic_layout);

        DynamicLayoutSpec spec = T::layout_spec_for(args...);
        uint32_t value_offset_in_words = T::static_value_offset_in_words();
        assert(expanded_layout_fits(value_offset_in_words));

        size_t object_size_in_bytes =
            size_t(spec.object_size_in_16byte_units) * 16;
        if(compact_layout_fits(spec.object_size_in_16byte_units,
                               value_offset_in_words, spec.value_count))
        {
            HeapLayout layout = encode_compact_layout_unchecked(
                spec.object_size_in_16byte_units, value_offset_in_words,
                spec.value_count);
            HeapAllocation allocation = allocate_fn(object_size_in_bytes);
            T *obj =
                new(allocation.memory) T(layout, std::forward<Args>(args)...);
            commit_heap_allocation(allocation, obj);
            return obj;
        }

        size_t allocation_size_in_bytes =
            sizeof(ExpandedHeader) + object_size_in_bytes;
        HeapAllocation allocation = allocate_fn(allocation_size_in_bytes);
        ExpandedHeader *header =
            reinterpret_cast<ExpandedHeader *>(allocation.memory);
        header->object_size_in_16byte_units = spec.object_size_in_16byte_units;
        header->value_count = spec.value_count;

        HeapLayout layout =
            encode_expanded_layout_unchecked(value_offset_in_words);
        T *obj = new(allocation.memory + sizeof(ExpandedHeader))
            T(layout, std::forward<Args>(args)...);
        commit_heap_allocation(allocation, obj);
        return obj;
    }

    static constexpr size_t DefaultSlabSize = 65536;
    static constexpr size_t LargeAllocationSize = DefaultSlabSize / 2;

    class ThreadLocalHeap;
    class HeapObject;

    /* global heap, shared between threads */

    class GlobalHeap
    {
    public:
        GlobalHeap(size_t _offset, size_t _slab_size);
        ~GlobalHeap();

        static GlobalHeap refcounted_heap(size_t slab_size = DefaultSlabSize)
        {
            return GlobalHeap(value_refcounted_ptr_tag, slab_size);
        }

        static GlobalHeap interned_heap(size_t slab_size = DefaultSlabSize)
        {
            return GlobalHeap(value_interned_ptr_tag, slab_size);
        }

        HeapAllocation allocate_large_object(
            size_t n_bytes);  // slow path allocation for large objects

        HeapAllocation allocate_global(size_t n_bytes);

        template <typename T, typename... Args>
        T *make_global_internal_raw(Args &&...args)
        {
            static_assert(std::is_base_of_v<HeapObject, T>);
            if constexpr(HasObjectLayout<T>::value && T::has_dynamic_layout)
            {
                return construct_dynamic_object<T>(
                    [this](size_t n_bytes) { return allocate_global(n_bytes); },
                    std::forward<Args>(args)...);
            }
            else
            {
                HeapAllocation allocation = allocate_global(sizeof(T));
                T *obj = new(allocation.memory) T(std::forward<Args>(args)...);
                commit_heap_allocation(allocation, obj);
                return obj;
            }
        }

        template <typename T, typename... Args>
        TValue<T> make_global_internal_value(Args &&...args)
        {
            return TValue<T>::from_oop(
                make_global_internal_raw<T>(std::forward<Args>(args)...));
        }

        SlabAllocator *make_new_slab();

        // Caller must ensure that this heap's slab lookup map is stable, for
        // example during safepoint reclamation or while holding heap_mutex.
        SlabAllocator *slab_for_address_unlocked(const void *ptr) const;
        SlabAllocator *slab_for_object_unlocked(const HeapObject *obj) const
        {
            return slab_for_address_unlocked(obj);
        }

    private:
        friend class SlabAllocator;

        void register_slab_pages_locked(SlabAllocator *slab);
        void unregister_slab_pages_locked(SlabAllocator *slab);
        SlabAllocator *make_new_slab(size_t actual_slab_size);
        void reclaim_slab(SlabAllocator *slab);

        std::mutex heap_mutex;
        std::deque<std::unique_ptr<SlabAllocator>> slabs;
        std::unordered_map<uintptr_t, SlabAllocator *> slab_lookup;
        size_t offset;
        size_t slab_size;
        std::unique_ptr<ThreadLocalHeap> global_allocator;
        std::mutex global_allocator_mutex;
    };

    /* thread local heap, for fast lockless allocation in the common case */
    class ThreadLocalHeap
    {
    public:
        ThreadLocalHeap(GlobalHeap *_global_heap);
        ~ThreadLocalHeap();

        // allocation fast path
        HeapAllocation allocate(size_t n_bytes)
        {
            if(n_bytes >= LargeAllocationSize)
            {
                return global_heap->allocate_large_object(n_bytes);
            }

            char *result = local_allocator->allocate(n_bytes);
            if(likely(result != nullptr))
            {
                return HeapAllocation{result, local_allocator};
            }

            SlabAllocator *old_allocator = local_allocator;
            SlabAllocator *new_allocator = global_heap->make_new_slab();
            local_allocator = new_allocator;
            add_allocator_reclaim_blocker();
            drop_allocator_reclaim_blocker(old_allocator);
            char *memory = local_allocator->allocate(n_bytes);
            assert(memory != nullptr);
            return HeapAllocation{memory, local_allocator};
        }

        template <typename T, typename... Args> T *make(Args &&...args)
        {
            static_assert(std::is_base_of_v<HeapObject, T>);
            static_assert(HasObjectLayout<T>::value);
            if constexpr(T::has_dynamic_layout)
            {
                return construct_dynamic_object<T>(
                    [this](size_t n_bytes) { return allocate(n_bytes); },
                    std::forward<Args>(args)...);
            }
            else
            {
                HeapAllocation allocation = allocate(sizeof(T));
                T *obj = new(allocation.memory) T(std::forward<Args>(args)...);
                commit_heap_allocation(allocation, obj);
                return obj;
            }
        }

    private:
        void add_allocator_reclaim_blocker();
        void drop_allocator_reclaim_blocker();
        void drop_allocator_reclaim_blocker(SlabAllocator *allocator);

        GlobalHeap *global_heap;
        SlabAllocator *local_allocator;
    };

}  // namespace cl

#endif  // CL_HEAP_H
