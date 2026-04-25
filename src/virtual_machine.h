#ifndef CL_VIRTUAL_MACHINE_H
#define CL_VIRTUAL_MACHINE_H

#include <memory>
#include <vector>

#include "builtin_function.h"
#include "heap.h"
#include "intern_store.h"
#include "owned_typed_value.h"
#include "scope.h"
#include "str.h"
#include "value.h"

namespace cl
{
    class ThreadState;

    class VirtualMachine
    {
    public:
        VirtualMachine();
        ~VirtualMachine();

        ThreadState *get_default_thread() { return threads[0].get(); }

        ThreadState *make_new_thread();

        GlobalHeap &get_refcounted_global_heap()
        {
            return refcounted_global_heap;
        }
        GlobalHeap &get_interned_global_heap() { return interned_global_heap; }

        template <typename Source>
        TValue<String> get_or_create_interned_string_value(const Source &str)
        {
            return interned_strings.get_or_create_value(str);
        }

        template <typename Source>
        String *get_or_create_interned_string_raw(const Source &str)
        {
            return interned_strings.get_or_create_raw(str);
        }

        TValue<Scope> get_builtin_scope() const { return builtin_scope; }
        TValue<BuiltinFunction> get_range_builtin() const
        {
            return range_builtin;
        }

    private:
        void initialize_builtin_scope();

        std::vector<std::unique_ptr<ThreadState>> threads;
        GlobalHeap refcounted_global_heap;
        GlobalHeap interned_global_heap;
        InternStore<std::wstring, String> interned_strings;
        OwnedTValue<Scope> builtin_scope;
        OwnedTValue<BuiltinFunction> range_builtin;
    };

}  // namespace cl

#endif  // CL_VIRTUAL_MACHINE_H
