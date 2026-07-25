#include "jit/jit_compiler.h"

#include "bytecode/code_object.h"
#include "jit/aarch64_backend.h"
#include "jit/compilation_session.h"
#include "jit/core_bytecode_translator.h"
#include "jit/core_ir_optimization.h"
#include "jit/graph_builder.h"
#include "jit/jit_code_object.h"
#include "runtime/thread_state.h"

#include <utility>

namespace cl::jit
{
    Result<JitCodeObject *, JitCompilationError>
    compile_jit_code(ThreadState &thread, const CodeObject &code_object,
                     const JitCompilerOptions &options)
    {
        if(options.observer != nullptr)
        {
            options.observer->on_bytecode(code_object);
        }

        CompilationSession session;
        GraphBuilder builder(session);
        CoreBytecodeTranslator translator(code_object, builder);
        ControlFlowGraph *graph = translator.translate();
        if(options.observer != nullptr)
        {
            options.observer->on_core_ir_translated(*graph);
        }

        auto optimization = optimize_core_ir(session, *graph);
        if(!optimization)
        {
            return Result<JitCodeObject *, JitCompilationError>::error(
                std::move(optimization).error());
        }
        if(options.observer != nullptr)
        {
            options.observer->on_core_ir_optimized(*graph);
        }

        auto code_result =
            compile_to_aarch64(session, *graph, thread.code_cache());
        if(!code_result)
        {
            return Result<JitCodeObject *, JitCompilationError>::error(
                std::move(code_result).error());
        }
        PublishedCode code = std::move(code_result).value();
        if(options.observer != nullptr)
        {
            options.observer->on_machine_code(code);
        }

        JitCodeObject *result = thread.make_internal_raw<JitCodeObject>(
            code.code(), code.value_pool_values(), code.encoded_code_size());
        return Result<JitCodeObject *, JitCompilationError>::ok(result);
    }

}  // namespace cl::jit
