#include "object_model/refcount.h"
#include "runtime/thread_state.h"

namespace cl
{

    void add_to_active_zero_count_table_if_needed(HeapObject *obj)
    {
        ThreadState::add_to_active_zero_count_table_if_needed(obj);
    }
}  // namespace cl
