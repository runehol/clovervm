#include "heap_reclamation.h"

#include "global_heap.h"
#include "slab_allocator.h"
#include "thread_state.h"
#include "virtual_machine.h"

#include <cassert>
#include <cstdint>

namespace cl
{
    namespace
    {
        struct HeapScanDescriptor
        {
            uint32_t first_value_offset_in_words;
            uint32_t value_count;
        };

        HeapScanDescriptor
        heap_scan_descriptor_for_object(const HeapObject *obj)
        {
            assert(obj != nullptr);
            if(layout_is_expanded(obj->layout))
            {
                return HeapScanDescriptor{
                    obj->layout & ~object_layout_expanded_bit,
                    uint32_t(expanded_header_for_object(obj)->value_count)};
            }

            return HeapScanDescriptor{
                compact_layout_value_offset_in_words(obj->layout),
                compact_layout_value_count(obj->layout)};
        }

        Value *heap_first_value_slot(HeapObject *obj,
                                     HeapScanDescriptor descriptor)
        {
            assert(obj != nullptr);
            return reinterpret_cast<Value *>(
                reinterpret_cast<uint64_t *>(obj) +
                descriptor.first_value_offset_in_words);
        }

        bool has_reclamation_descriptor(HeapObject *obj)
        {
            return !layout_is_expanded(obj->layout);
        }

        class ReclamationContext
        {
        public:
            ReclamationContext(GlobalHeap &heap, std::vector<HeapObject *> &zct)
                : refcounted_heap(heap), current_zct(zct)
            {
            }

            void reclaim_object(HeapObject *obj)
            {
                assert(obj != nullptr);
                assert(obj->lifecycle_state == HeapLifecycleState::Reclaiming);
                assert(has_reclamation_descriptor(obj));
                reclaim_object_value_slots(obj);
                SlabAllocator *slab =
                    refcounted_heap.slab_for_object_unlocked(obj);
                obj->lifecycle_state = HeapLifecycleState::Dead;
                slab->drop_reclaim_blocker();
            }

        private:
            void add_to_current_zero_count_table_if_needed(HeapObject *obj)
            {
                assert(obj != nullptr);
                assert((reinterpret_cast<uintptr_t>(obj) & value_ptr_mask) ==
                       value_refcounted_ptr_tag);
                assert(obj->lifecycle_state != HeapLifecycleState::Reclaiming);
                assert(obj->lifecycle_state != HeapLifecycleState::Dead);
                if(obj->lifecycle_state == HeapLifecycleState::InZct)
                {
                    return;
                }

                assert(obj->lifecycle_state == HeapLifecycleState::Normal);
                assert(obj->refcount == 0);
                obj->lifecycle_state = HeapLifecycleState::InZct;
                current_zct.push_back(obj);
            }

            void release_value(Value value)
            {
                if(!value.is_refcounted_ptr())
                {
                    return;
                }

                HeapObject *child = value.as.ptr;
                if(--child->refcount == 0)
                {
                    add_to_current_zero_count_table_if_needed(child);
                }
            }

            void reclaim_object_value_slots(HeapObject *obj)
            {
                HeapScanDescriptor descriptor =
                    heap_scan_descriptor_for_object(obj);
                Value *slots = heap_first_value_slot(obj, descriptor);
                for(uint32_t idx = 0; idx < descriptor.value_count; ++idx)
                {
                    Value value = slots[idx];
                    slots[idx] = Value::not_present();
                    release_value(value);
                }
            }

            GlobalHeap &refcounted_heap;
            std::vector<HeapObject *> &current_zct;
        };
    }  // namespace

    void collect_reclamation_roots_from_thread(ReclamationRootSet &roots,
                                               const ThreadState &thread)
    {
        const SafepointScanRecord &record = thread.safepoint_scan_record();
        Value *lowest_live_stack_slot = record.lowest_live_stack_slot;
        Value *sentinel = thread.clover_frame_sentinel();

        assert(lowest_live_stack_slot != nullptr);
        assert(lowest_live_stack_slot >= thread.clover_stack_begin());
        assert(lowest_live_stack_slot <= sentinel);
        assert(sentinel < thread.clover_stack_end());

        for(Value *slot = lowest_live_stack_slot; slot < sentinel; ++slot)
        {
            roots.add_conservative_value(*slot);
        }

        roots.add_conservative_value(record.accumulator_or_not_present);
    }

    ReclamationRootSet
    collect_reclamation_roots_from_threads(const ThreadStateList &threads)
    {
        ReclamationRootSet roots;
        for(const std::unique_ptr<ThreadState> &thread: threads)
        {
            collect_reclamation_roots_from_thread(roots, *thread);
        }
        return roots;
    }

    void
    process_zero_count_table_for_reclamation(ThreadState &thread,
                                             const ReclamationRootSet &roots)
    {
        std::vector<HeapObject *> &zero_count_table = thread.zero_count_table;
        ReclamationContext reclamation_context(
            thread.get_machine()->get_refcounted_global_heap(),
            zero_count_table);
        size_t scan = 0;
        size_t keep = 0;
        while(scan < zero_count_table.size())
        {
            HeapObject *obj = zero_count_table[scan++];
            assert(obj != nullptr);
            assert(obj->lifecycle_state == HeapLifecycleState::InZct);

            if(obj->refcount > 0)
            {
                obj->lifecycle_state = HeapLifecycleState::Normal;
                continue;
            }

            assert(obj->refcount == 0);
            if(roots.contains(obj))
            {
                zero_count_table[keep++] = obj;
                continue;
            }
            if(!has_reclamation_descriptor(obj))
            {
                zero_count_table[keep++] = obj;
                continue;
            }

            obj->lifecycle_state = HeapLifecycleState::Reclaiming;
            reclamation_context.reclaim_object(obj);
        }

        zero_count_table.resize(keep);
    }

    void run_heap_reclamation(const ThreadStateList &threads)
    {
        ReclamationRootSet roots =
            collect_reclamation_roots_from_threads(threads);
        for(const std::unique_ptr<ThreadState> &thread: threads)
        {
            process_zero_count_table_for_reclamation(*thread, roots);
        }
    }

}  // namespace cl
