#ifndef CL_JIT_COMPILATION_SESSION_H
#define CL_JIT_COMPILATION_SESSION_H

#include "jit/compilation_arena.h"

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

    private:
        CompilationArena arena_;
    };

}  // namespace cl::jit

#endif  // CL_JIT_COMPILATION_SESSION_H
