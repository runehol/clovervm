#ifndef CL_JIT_CFG_VERIFIER_H
#define CL_JIT_CFG_VERIFIER_H

#include <string>

namespace cl::jit
{
    class ControlFlowGraph;

    struct CfgVerificationResult
    {
        bool valid;
        std::string message;
    };

    [[nodiscard]] CfgVerificationResult
    verify_cfg(const ControlFlowGraph &graph);

}  // namespace cl::jit

#endif  // CL_JIT_CFG_VERIFIER_H
