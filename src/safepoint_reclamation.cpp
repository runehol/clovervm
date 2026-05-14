#include "safepoint_reclamation.h"

#include "thread_state.h"

#include <cassert>

namespace cl
{
    void collect_safepoint_roots_from_thread(SafepointRootSet &roots,
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

    SafepointRootSet
    collect_safepoint_roots_from_threads(const ThreadStateList &threads)
    {
        SafepointRootSet roots;
        for(const std::unique_ptr<ThreadState> &thread: threads)
        {
            collect_safepoint_roots_from_thread(roots, *thread);
        }
        return roots;
    }

    void process_zero_count_table_for_safepoint(ThreadState &thread,
                                                const SafepointRootSet &roots)
    {
        std::vector<HeapObject *> &zero_count_table = thread.zero_count_table;
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

            // Object teardown is wired in the next Phase 6 slice. Until then,
            // retain unrooted candidates in the ZCT rather than losing them.
            zero_count_table[keep++] = obj;
        }

        zero_count_table.resize(keep);
    }

    void run_safepoint_reclamation(const ThreadStateList &threads)
    {
        SafepointRootSet roots = collect_safepoint_roots_from_threads(threads);
        for(const std::unique_ptr<ThreadState> &thread: threads)
        {
            process_zero_count_table_for_safepoint(*thread, roots);
        }
    }

}  // namespace cl
