#ifndef CL_SCOPED_ROOT_H
#define CL_SCOPED_ROOT_H

#include "memory/heap_reference_slot.h"
#include "runtime/thread_state.h"

#include <cassert>

namespace cl
{
    // Borrows one initialized, writable, address-stable native cell. The cell
    // and thread must outlive this scope. Ownership is independent of rooting;
    // callers must reload this cell after a safepoint, not reuse an extracted
    // pointer. Registration and removal do not allocate, retain, or release.
    class ScopedRoot
    {
    public:
        ScopedRoot(ThreadState &thread, HeapReferenceSlot slot)
            : thread_(&thread), previous_(thread.native_roots_), slot_(slot)
        {
            thread.native_roots_ = this;
        }

        ~ScopedRoot()
        {
            assert(thread_->native_roots_ == this);
            thread_->native_roots_ = previous_;
        }

        ScopedRoot(const ScopedRoot &) = delete;
        ScopedRoot &operator=(const ScopedRoot &) = delete;
        ScopedRoot(ScopedRoot &&) = delete;
        ScopedRoot &operator=(ScopedRoot &&) = delete;

    private:
        ThreadState *thread_;
        ScopedRoot *previous_;
        HeapReferenceSlot slot_;

        friend class ThreadState;
    };
}  // namespace cl

#endif  // CL_SCOPED_ROOT_H
