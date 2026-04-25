#include "refcount.h"
#include "thread_state.h"

namespace cl
{

    void add_to_active_zero_count_table(HeapObject *obj)
    {
        ThreadState::add_to_active_zero_count_table(obj);
    }
}  // namespace cl
