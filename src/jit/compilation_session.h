#ifndef CL_JIT_COMPILATION_SESSION_H
#define CL_JIT_COMPILATION_SESSION_H

#include "jit/compilation_storage.h"
#include "object_model/owned.h"

#include <vector>

namespace cl
{
    class ThreadState;
}

namespace cl::jit
{
    // Owns all resources whose lifetime is one compilation. The storage remains
    // a separate storage mechanism: callers may borrow its handle, but only
    // its privileged construction APIs can allocate from it.
    class CompilationSession
    {
    public:
        explicit CompilationSession(ThreadState &thread) : thread_(thread) {}

        CompilationSession(const CompilationSession &) = delete;
        CompilationSession &operator=(const CompilationSession &) = delete;
        CompilationSession(CompilationSession &&) = delete;
        CompilationSession &operator=(CompilationSession &&) = delete;

        CompilationStorage *storage() { return &storage_; }
        const CompilationStorage *storage() const { return &storage_; }
        ThreadState &thread_state() const { return thread_; }

        template <typename T> T retain_and_pin_value(T value)
        {
            Value raw = value.raw_value();
            if(raw.is_ptr())
            {
                retained_values_.emplace_back(raw);
            }
            return value;
        }

    private:
        // Members are destroyed in reverse order, so storage-owned compiler
        // state is released before the values it may reference.
        std::vector<Owned<Value>> retained_values_;
        ThreadState &thread_;
        CompilationStorage storage_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_SESSION_H
