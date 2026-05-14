#include "safepoint_roots.h"

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

}  // namespace cl
