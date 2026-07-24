#ifndef CL_JIT_IR_PRINT_H
#define CL_JIT_IR_PRINT_H

#include <fmt/format.h>

#include <string>

namespace cl::jit
{
    class ControlFlowGraph;
    class Instruction;

    std::string format_ir(const ControlFlowGraph &graph);
    std::string format_instruction(const ControlFlowGraph &graph,
                                   const Instruction &instruction);

}  // namespace cl::jit

template <> struct fmt::formatter<cl::jit::ControlFlowGraph>
{
    constexpr auto parse(format_parse_context &ctx) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const cl::jit::ControlFlowGraph &graph,
                FormatContext &ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "{}", cl::jit::format_ir(graph));
    }
};

#endif  // CL_JIT_IR_PRINT_H
