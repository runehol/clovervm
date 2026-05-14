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
        struct ObjectValueSpan
        {
            Value *slots;
            uint64_t count;
        };

        ObjectValueSpan object_value_span_for(HeapObject *obj)
        {
            assert(obj != nullptr);
            uint32_t first_value_offset_in_words;
            uint64_t value_count;
            if(layout_is_expanded(obj->layout))
            {
                first_value_offset_in_words =
                    obj->layout & ~object_layout_expanded_bit;
                value_count = expanded_header_for_object(obj)->value_count;
            }
            else
            {
                first_value_offset_in_words =
                    compact_layout_value_offset_in_words(obj->layout);
                value_count = compact_layout_value_count(obj->layout);
            }

            return ObjectValueSpan{reinterpret_cast<Value *>(obj) +
                                       first_value_offset_in_words,
                                   value_count};
        }

        class ReclamationContext
        {
        public:
            ReclamationContext(GlobalHeap &heap, std::vector<HeapObject *> &zct)
                : refcounted_heap(heap), current_zct(zct)
            {
            }

            void reclaim_object(HeapObject *obj, ObjectValueSpan value_span)
            {
                assert(obj != nullptr);
                assert(obj->lifecycle_state == HeapLifecycleState::Reclaiming);
                reclaim_object_value_slots(value_span);
                SlabAllocator *slab =
                    refcounted_heap.slab_for_object_unlocked(obj);
                obj->lifecycle_state = HeapLifecycleState::Dead;
                slab->clear_valid_object(obj);
                remember_release_candidate(slab);
            }

            void reclaim_object(HeapObject *obj)
            {
                reclaim_object(obj, object_value_span_for(obj));
            }

            void remember_release_candidate(SlabAllocator *slab)
            {
                bool inserted = release_candidate_set.insert(slab).second;
                if(inserted)
                {
                    release_candidates.push_back(slab);
                }
            }

            void release_empty_candidate_slabs()
            {
                for(SlabAllocator *slab: release_candidates)
                {
                    refcounted_heap.release_slab_if_empty(slab);
                }
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

            void reclaim_object_value_slots(ObjectValueSpan value_span)
            {
                for(uint64_t idx = 0; idx < value_span.count; ++idx)
                {
                    Value value = value_span.slots[idx];
                    value_span.slots[idx] = Value::not_present();
                    release_value(value);
                }
            }

            GlobalHeap &refcounted_heap;
            std::vector<HeapObject *> &current_zct;
            absl::flat_hash_set<SlabAllocator *> release_candidate_set;
            std::vector<SlabAllocator *> release_candidates;
        };

        void process_zero_count_table_entries(
            std::vector<HeapObject *> &zero_count_table, ThreadState &thread,
            const ReclamationRootSet &roots, ReclamationContext &context)
        {
#ifndef NDEBUG
            validate_zero_count_table_for_reclamation(thread);
#endif
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
                ObjectValueSpan value_span = object_value_span_for(obj);

                obj->lifecycle_state = HeapLifecycleState::Reclaiming;
                context.reclaim_object(obj, value_span);
            }

            zero_count_table.resize(keep);
        }

        void scan_epoch_slab_bitmaps(
            ThreadLocalHeap &heap, std::vector<HeapObject *> &zero_count_table,
            const ReclamationRootSet &roots, ReclamationContext &context)
        {
            heap.for_each_epoch_slab(
                [&zero_count_table, &roots, &context](SlabAllocator *slab) {
                    slab->for_each_valid_object([&zero_count_table, &roots,
                                                 &context](HeapObject *obj) {
                        assert(obj != nullptr);
                        if(obj->refcount > 0)
                        {
                            return;
                        }
                        if(obj->lifecycle_state != HeapLifecycleState::Normal)
                        {
                            return;
                        }

                        assert(obj->refcount == 0);
                        if(roots.contains(obj))
                        {
                            obj->lifecycle_state = HeapLifecycleState::InZct;
                            zero_count_table.push_back(obj);
                            return;
                        }

                        obj->lifecycle_state = HeapLifecycleState::Reclaiming;
                        context.reclaim_object(obj);
                    });
                });
        }
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

#ifndef NDEBUG
    void validate_zero_count_table_for_reclamation(const ThreadState &thread)
    {
        absl::flat_hash_set<HeapObject *> seen;
        for(HeapObject *obj: thread.zero_count_table)
        {
            assert(obj != nullptr);
            assert(obj->lifecycle_state == HeapLifecycleState::InZct);
            bool inserted = seen.insert(obj).second;
            assert(inserted && "duplicate heap object in zero count table");
        }
    }

    void
    validate_zero_count_tables_for_reclamation(const ThreadStateList &threads)
    {
        absl::flat_hash_set<HeapObject *> seen;
        for(const std::unique_ptr<ThreadState> &thread: threads)
        {
            validate_zero_count_table_for_reclamation(*thread);
            for(HeapObject *obj: thread->zero_count_table)
            {
                bool inserted = seen.insert(obj).second;
                assert(inserted && "duplicate heap object in zero count table");
            }
        }
    }
#endif

    void
    process_zero_count_table_for_reclamation(ThreadState &thread,
                                             const ReclamationRootSet &roots)
    {
        std::vector<HeapObject *> &zero_count_table = thread.zero_count_table;
        ReclamationContext reclamation_context(
            thread.get_machine()->get_refcounted_global_heap(),
            zero_count_table);
        process_zero_count_table_entries(zero_count_table, thread, roots,
                                         reclamation_context);
        reclamation_context.release_empty_candidate_slabs();
    }

    void scan_epoch_slabs_for_reclamation(ThreadState &thread,
                                          const ReclamationRootSet &roots)
    {
        std::vector<HeapObject *> &zero_count_table = thread.zero_count_table;
        ReclamationContext reclamation_context(
            thread.get_machine()->get_refcounted_global_heap(),
            zero_count_table);
        scan_epoch_slab_bitmaps(thread.refcounted_heap, zero_count_table, roots,
                                reclamation_context);
        reclamation_context.release_empty_candidate_slabs();
    }

    void process_thread_reclamation_epoch(ThreadState &thread,
                                          const ReclamationRootSet &roots)
    {
        std::vector<HeapObject *> &zero_count_table = thread.zero_count_table;
        ReclamationContext reclamation_context(
            thread.get_machine()->get_refcounted_global_heap(),
            zero_count_table);
        process_zero_count_table_entries(zero_count_table, thread, roots,
                                         reclamation_context);
        scan_epoch_slab_bitmaps(thread.refcounted_heap, zero_count_table, roots,
                                reclamation_context);
        std::vector<SlabAllocator *> finished_epoch =
            thread.refcounted_heap.finish_reclamation_epoch();
        for(SlabAllocator *slab: finished_epoch)
        {
            slab->drop_epoch_discovery_pin();
            reclamation_context.remember_release_candidate(slab);
        }
        reclamation_context.release_empty_candidate_slabs();
    }

    void run_heap_reclamation(const ThreadStateList &threads)
    {
#ifndef NDEBUG
        validate_zero_count_tables_for_reclamation(threads);
#endif
        ReclamationRootSet roots =
            collect_reclamation_roots_from_threads(threads);
        for(const std::unique_ptr<ThreadState> &thread: threads)
        {
            process_thread_reclamation_epoch(*thread, roots);
        }
    }

}  // namespace cl
