#include "gc/evacuation.h"

#include <cstdlib>

#ifdef __cpp_exceptions
#error "This compile check must build with C++ exceptions disabled"
#endif

namespace cl::gc::test
{
    // Compile-only adapter: explicitly instantiate every collector operation
    // under the runtime's exception flags, without the throwing test heap.
    struct NoExceptionsHeap
    {
        using Object = int;
        using WeakUnit = WeakRepairUnit<Object>;

        struct Slot
        {
            Object *load_pointer() const { return nullptr; }
            void rewrite_pointer(Object *) const {}
        };

        bool is_collected(Object *) const { return false; }
        bool is_collected_source(Object *) const { return false; }
        bool try_mark(Object *) { return false; }
        bool is_marked(Object *) const { return false; }
        Object *forwarded_to(Object *) const { return nullptr; }
        Object *copy_object(Object *) { return nullptr; }
        void install_forwarding(Object *, Object *) {}
        [[noreturn]] void allocation_failure() { std::abort(); }

        template <typename Visitor>
        void visit_strong_slots(Object *, Visitor &&)
        {
        }

        template <typename Visitor> void visit_weak_units(Object *, Visitor &&)
        {
        }
    };
}  // namespace cl::gc::test

template class cl::gc::Evacuation<cl::gc::test::NoExceptionsHeap>;
