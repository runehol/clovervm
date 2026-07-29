#ifndef CL_JIT_JIT_COMPILER_H
#define CL_JIT_JIT_COMPILER_H

#include "jit/jit_compilation_error.h"
#include "util/result.h"

namespace cl
{
    class CodeObject;
    class ThreadState;

    namespace jit
    {
        class ControlFlowGraph;
        class JitCodeObject;
        class PublishedCode;

        class JitCompilationObserver
        {
        public:
            virtual ~JitCompilationObserver() = default;

            virtual void on_bytecode(const CodeObject &) {}
            virtual void on_core_ir_translated(const ControlFlowGraph &) {}
            virtual void on_core_ir_optimized(const ControlFlowGraph &) {}
            virtual void on_machine_ir(const ControlFlowGraph &) {}
            virtual void on_machine_code(const PublishedCode &) {}
        };

        struct JitCompilerOptions
        {
            JitCompilationObserver *observer = nullptr;
        };

        [[nodiscard]] Result<JitCodeObject *, JitCompilationError>
        compile_jit_code(ThreadState &thread, const CodeObject &code_object,
                         const JitCompilerOptions &options = {});

    }  // namespace jit
}  // namespace cl

#endif  // CL_JIT_JIT_COMPILER_H
