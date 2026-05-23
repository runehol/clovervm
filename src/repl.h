#ifndef CL_REPL_H
#define CL_REPL_H

namespace cl
{
    class VirtualMachine;

    int run_repl(VirtualMachine &vm, bool print_bytecode);
}  // namespace cl

#endif  // CL_REPL_H
