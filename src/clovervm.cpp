#include <clocale>
#include <clovervm/clovervm.h>
#include <iostream>
#include <locale>
#include <string>

int main(int argc, const char *argv[])
{
    std::setlocale(LC_CTYPE, "");
    std::locale::global(std::locale(""));
    std::wcout.imbue(std::locale());
    std::wcerr.imbue(std::locale());

    using namespace std::literals;
    bool print_bytecode = false;
    bool trace_instructions = false;
    const char *source_file = nullptr;
    int i = 1;
    for(; i < argc; ++i)
    {
        if(argv[i] == "--print-bytecode"sv)
        {
            print_bytecode = true;
        }
        else if(argv[i] == "--trace-instructions"sv)
        {
            trace_instructions = true;
        }
        else
        {
            break;
        }
    }

    if(i < argc)
    {
        source_file = argv[i++];
    }

    if(source_file == nullptr)
    {
        clover_vm *vm = clover_vm_new();
        if(vm == nullptr)
        {
            std::cerr << "failed to create clover VM\n";
            return 1;
        }
        clover_vm_set_trace_instructions(vm, trace_instructions);
        clover_status status = clover_vm_run_interactive(vm, print_bytecode);
        clover_vm_destroy(vm);
        return status == CLOVER_STATUS_OK ? 0 : 1;
    }

    clover_vm *vm = clover_vm_new();
    if(vm == nullptr)
    {
        std::cerr << "failed to create clover VM\n";
        return 1;
    }
    clover_vm_set_trace_instructions(vm, trace_instructions);
    clover_status status = clover_vm_run_file(vm, source_file, print_bytecode);
    clover_vm_destroy(vm);
    return status == CLOVER_STATUS_OK ? 0 : 1;
}
