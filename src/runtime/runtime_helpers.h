#ifndef CL_RUNTIME_HELPERS_H
#define CL_RUNTIME_HELPERS_H

#include "builtin_types/str.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

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

    template <typename Source>
    Owned<TValue<String>> string_value(ThreadState *thread, const Source &str)
    {
        return Owned<TValue<String>>(thread->make_object_value<String>(str));
    }

    template <typename Source>
    Owned<TValue<String>> string_value(const Source &str)
    {
        return string_value(active_thread(), str);
    }
}  // namespace cl

#endif  // CL_RUNTIME_HELPERS_H
