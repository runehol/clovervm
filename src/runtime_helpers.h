#ifndef CL_RUNTIME_HELPERS_H
#define CL_RUNTIME_HELPERS_H

#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"

namespace cl
{
    inline VirtualMachine *active_vm()
    {
        return active_thread()->get_machine();
    }

    template <typename Source> TValue<String> interned_string(const Source &str)
    {
        return active_vm()->get_or_create_interned_string_value(str);
    }
}  // namespace cl

#endif  // CL_RUNTIME_HELPERS_H
