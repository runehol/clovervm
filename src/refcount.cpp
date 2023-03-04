#include "refcount.h"
#include "thread_state.h"

namespace cl
{

    void add_to_active_zero_count_table(Value v)
    {
        ThreadState::add_to_active_zero_count_table(v);
    }
}
