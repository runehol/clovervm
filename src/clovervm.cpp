#include <clocale>
#include <clovervm/clovervm.h>
#include <cxxopts.hpp>
#include <iostream>
#include <locale>
#include <string>

namespace
{
    struct CommandLine
    {
        bool print_bytecode = false;
        bool trace_instructions = false;
        bool show_help = false;
        bool has_command = false;
        std::string command;
        std::string source_file;
    };

    CommandLine parse_command_line(int argc, const char *argv[],
                                   cxxopts::Options &options)
    {
        options.add_options()("h,help", "Print help and exit")(
            "print-bytecode", "Print bytecode before execution")(
            "trace-instructions", "Trace interpreter instructions")(
            "c", "Program passed in as a string", cxxopts::value<std::string>(),
            "COMMAND")("source_file", "Python source file",
                       cxxopts::value<std::vector<std::string>>(), "FILE");
        options.parse_positional({"source_file"});
        options.positional_help("[FILE]");

        cxxopts::ParseResult result = options.parse(argc, argv);
        CommandLine command_line;
        command_line.print_bytecode = result.count("print-bytecode") != 0;
        command_line.trace_instructions =
            result.count("trace-instructions") != 0;
        command_line.show_help = result.count("help") != 0;

        if(result.count("c") != 0)
        {
            command_line.has_command = true;
            command_line.command = result["c"].as<std::string>();
        }

        if(result.count("source_file") != 0)
        {
            const std::vector<std::string> source_files =
                result["source_file"].as<std::vector<std::string>>();
            if(source_files.size() > 1)
            {
                throw cxxopts::exceptions::exception(
                    "only one source file is supported");
            }
            command_line.source_file = source_files.front();
        }

        if(command_line.has_command && !command_line.source_file.empty())
        {
            throw cxxopts::exceptions::exception(
                "cannot specify both -c and a source file");
        }

        return command_line;
    }
}  // namespace

int main(int argc, const char *argv[])
{
    std::setlocale(LC_CTYPE, "");
    std::locale::global(std::locale(""));
    std::wcout.imbue(std::locale());
    std::wcerr.imbue(std::locale());

    cxxopts::Options options("clovervm", "A Python VM");
    CommandLine command_line;
    try
    {
        command_line = parse_command_line(argc, argv, options);
    }
    catch(const cxxopts::exceptions::exception &err)
    {
        std::cerr << "clovervm: " << err.what()
                  << "\nTry 'clovervm --help' for more information.\n";
        return 1;
    }

    if(command_line.show_help)
    {
        std::cout << options.help() << "\n";
        return 0;
    }

    if(!command_line.has_command && command_line.source_file.empty())
    {
        clover_vm *vm = clover_vm_new();
        if(vm == nullptr)
        {
            std::cerr << "failed to create clover VM\n";
            return 1;
        }
        clover_vm_set_trace_instructions(vm, command_line.trace_instructions);
        clover_status status =
            clover_vm_run_interactive(vm, command_line.print_bytecode);
        clover_vm_destroy(vm);
        return status == CLOVER_STATUS_OK ? 0 : 1;
    }

    clover_vm *vm = clover_vm_new();
    if(vm == nullptr)
    {
        std::cerr << "failed to create clover VM\n";
        return 1;
    }
    clover_vm_set_trace_instructions(vm, command_line.trace_instructions);
    clover_status status =
        command_line.has_command
            ? clover_vm_run_string(vm, command_line.command.c_str(),
                                   command_line.print_bytecode)
            : clover_vm_run_file(vm, command_line.source_file.c_str(),
                                 command_line.print_bytecode);
    clover_vm_destroy(vm);
    return status == CLOVER_STATUS_OK ? 0 : 1;
}
