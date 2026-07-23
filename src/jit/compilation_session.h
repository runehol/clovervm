#ifndef CL_JIT_COMPILATION_SESSION_H
#define CL_JIT_COMPILATION_SESSION_H

#include "jit/compilation_arena.h"
#include "object_model/owned.h"

#include <vector>

namespace cl::jit
{
    // Owns all resources whose lifetime is one compilation. The arena remains
    // a separate storage mechanism: callers may borrow its handle, but only
    // its privileged construction APIs can allocate from it.
    class CompilationSession
    {
    public:
        CompilationSession() = default;

        CompilationSession(const CompilationSession &) = delete;
        CompilationSession &operator=(const CompilationSession &) = delete;
        CompilationSession(CompilationSession &&) = delete;
        CompilationSession &operator=(CompilationSession &&) = delete;

        CompilationArena &arena() { return arena_; }
        const CompilationArena &arena() const { return arena_; }

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
        // Members are destroyed in reverse order, so arena-owned compiler
        // state is released before the values it may reference.
        std::vector<Owned<Value>> retained_values_;
        CompilationArena arena_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_SESSION_H
