#include "builtin_types/str.h"
#include "builtin_types/unicode.h"
#include "bytecode/code_object.h"
#include "bytecode/code_object_print.h"
#include "compiler/parser.h"
#include "compiler/source_text.h"
#include "jit/code_cache.h"
#include "jit/ir_print.h"
#include "jit/jit_compiler.h"
#include "object_model/object.h"
#include "runtime/exception_object.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

#include <cxxopts.hpp>
#include <fmt/core.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <spawn.h>
#include <unistd.h>

extern char **environ;

namespace
{
    struct CommandLine
    {
        bool show_help = false;
        bool has_command = false;
        std::string command;
        std::string source_file;
        std::string clang = "clang";
        std::string objdump = "objdump";
    };

    struct SourceInput
    {
        std::wstring source;
        std::optional<std::wstring> filename;
    };

    const char *
    jit_compilation_error_name(const cl::jit::JitCompilationError &error)
    {
        return std::visit(
            [](auto value) -> const char * {
                using Error = decltype(value);
                if constexpr(std::is_same_v<Error,
                                            cl::jit::RegisterAllocationError>)
                {
                    switch(value)
                    {
                        case cl::jit::RegisterAllocationError::
                            UnsupportedSnapshotConsumer:
                            return "UnsupportedSnapshotConsumer";
                        case cl::jit::RegisterAllocationError::
                            UnsupportedSameAsInput:
                            return "UnsupportedSameAsInput";
                        case cl::jit::RegisterAllocationError::
                            UnsupportedTransferPoint:
                            return "UnsupportedTransferPoint";
                        case cl::jit::RegisterAllocationError::
                            RequiresConstraintFixup:
                            return "RequiresConstraintFixup";
                        case cl::jit::RegisterAllocationError::
                            RequiresSplittingOrSpilling:
                            return "RequiresSplittingOrSpilling";
                        case cl::jit::RegisterAllocationError::
                            InsufficientTransferScratchRegisters:
                            return "InsufficientTransferScratchRegisters";
                    }
                }
                else
                {
                    static_assert(std::is_same_v<Error, cl::jit::JitCodeError>);
                    switch(value)
                    {
                        case cl::jit::JitCodeError::PoolOutOfRange:
                            return "PoolOutOfRange";
                        case cl::jit::JitCodeError::AllocationFailure:
                            return "AllocationFailure";
                        case cl::jit::JitCodeError::PublicationFailure:
                            return "PublicationFailure";
                    }
                }
                return "Unknown";
            },
            error);
    }

    CommandLine parse_command_line(int argc, const char *argv[],
                                   cxxopts::Options &options)
    {
        options.add_options()("h,help", "Print help and exit")(
            "c", "Python source passed as a string",
            cxxopts::value<std::string>(), "COMMAND")(
            "clang", "Clang executable used to wrap emitted code",
            cxxopts::value<std::string>()->default_value("clang"),
            "PATH")("objdump", "Objdump executable used for disassembly",
                    cxxopts::value<std::string>()->default_value("objdump"),
                    "PATH")("source_file", "Python source file",
                            cxxopts::value<std::vector<std::string>>(), "FILE");
        options.parse_positional({"source_file"});
        options.positional_help("[FILE]");

        cxxopts::ParseResult parsed = options.parse(argc, argv);
        CommandLine result;
        result.show_help = parsed.count("help") != 0;
        result.clang = parsed["clang"].as<std::string>();
        result.objdump = parsed["objdump"].as<std::string>();
        if(parsed.count("c") != 0)
        {
            result.has_command = true;
            result.command = parsed["c"].as<std::string>();
        }
        if(parsed.count("source_file") != 0)
        {
            std::vector<std::string> files =
                parsed["source_file"].as<std::vector<std::string>>();
            if(files.size() > 1)
            {
                throw cxxopts::exceptions::exception(
                    "only one source file is supported");
            }
            result.source_file = std::move(files.front());
        }
        if(result.has_command && !result.source_file.empty())
        {
            throw cxxopts::exceptions::exception(
                "cannot specify both -c and a source file");
        }
        if(!result.show_help && !result.has_command &&
           result.source_file.empty())
        {
            throw cxxopts::exceptions::exception(
                "a command or source file is required");
        }
        return result;
    }

    std::wstring cl_string_to_wstring(cl::TValue<cl::String> string)
    {
        cl::String *str = string.extract();
        return std::wstring(str->data, size_t(str->count.extract()));
    }

    std::wstring format_pending_python_exception(cl::ThreadState *thread)
    {
        if(thread->pending_exception_kind() ==
           cl::PendingExceptionKind::StopIteration)
        {
            return L"StopIteration";
        }
        if(thread->pending_exception_kind() != cl::PendingExceptionKind::Object)
        {
            return L"InternalError: exception marker without pending exception";
        }

        cl::TValue<cl::Exception> exception =
            thread->pending_exception_object();
        std::wstring result = cl_string_to_wstring(
            exception.extract()->get_shape()->get_class()->get_name());
        std::wstring message =
            cl_string_to_wstring(exception.extract()->message.value());
        if(!message.empty())
        {
            result += L": ";
            result += message;
        }
        return result;
    }

    std::optional<SourceInput> read_source(const CommandLine &command_line)
    {
        if(command_line.has_command)
        {
            std::optional<std::wstring> source =
                cl::decode_source_text(command_line.command);
            if(!source.has_value())
            {
                return std::nullopt;
            }
            return SourceInput{std::move(*source), std::nullopt};
        }
        std::optional<std::wstring> filename =
            cl::unicode::decode_utf8(command_line.source_file);
        if(!filename.has_value())
        {
            return std::nullopt;
        }
        std::optional<std::wstring> source =
            cl::read_source_text_file(*filename);
        if(!source.has_value())
        {
            return std::nullopt;
        }
        return SourceInput{std::move(*source), std::move(filename)};
    }

    cl::CodeObject *single_function_code(cl::CodeObject &module)
    {
        cl::CodeObject *result = nullptr;
        for(const auto &constant: module.constant_table)
        {
            cl::Value value = constant.value();
            if(!value.is_ptr() ||
               value.get_ptr<cl::Object>()->native_layout_id() !=
                   cl::NativeLayoutId::CodeObject)
            {
                continue;
            }
            if(result != nullptr)
            {
                return nullptr;
            }
            result = value.get_ptr<cl::CodeObject>();
        }
        return result;
    }

    int run_process(const std::vector<std::string> &arguments)
    {
        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for(const std::string &argument: arguments)
        {
            argv.push_back(const_cast<char *>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t child;
        int error = posix_spawnp(&child, argv[0], nullptr, nullptr, argv.data(),
                                 environ);
        if(error != 0)
        {
            fmt::print(stderr, "failed to run '{}': {}\n", arguments.front(),
                       std::strerror(error));
            return -1;
        }

        int status;
        while(waitpid(child, &status, 0) < 0)
        {
            if(errno != EINTR)
            {
                fmt::print(stderr, "failed waiting for '{}': {}\n",
                           arguments.front(), std::strerror(errno));
                return -1;
            }
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    std::optional<std::filesystem::path> make_temporary_directory()
    {
        std::string pattern = (std::filesystem::temp_directory_path() /
                               "clovervm-jit-dump.XXXXXX")
                                  .string();
        if(mkdtemp(pattern.data()) == nullptr)
        {
            return std::nullopt;
        }
        return std::filesystem::path(std::move(pattern));
    }

    std::vector<uint32_t> machine_words(const cl::jit::PublishedCode &code)
    {
        if(code.encoded_code_size() % sizeof(uint32_t) != 0)
        {
            return {};
        }
        const void *bytes = reinterpret_cast<const void *>(
            code.entry().bits_for_indirect_target());
        std::vector<uint32_t> result(code.encoded_code_size() /
                                     sizeof(uint32_t));
        std::memcpy(result.data(), bytes, code.encoded_code_size());
        return result;
    }

    bool print_disassembly(std::span<const uint32_t> words,
                           std::span<const cl::Value> pool_values,
                           size_t pool_offset, const CommandLine &command_line)
    {
        size_t code_size = words.size() * sizeof(uint32_t);
        if(!pool_values.empty() && pool_offset < code_size)
        {
            fmt::print(stderr, "AArch64 constant pool overlaps code\n");
            return false;
        }

        std::optional<std::filesystem::path> temporary =
            make_temporary_directory();
        if(!temporary.has_value())
        {
            fmt::print(stderr, "failed to create a temporary directory: {}\n",
                       std::strerror(errno));
            return false;
        }
        std::filesystem::path assembly = *temporary / "jit-function.s";
        std::filesystem::path object = *temporary / "jit-function.o";

        bool success = false;
        {
            std::ofstream out(assembly);
            if(!out)
            {
                fmt::print(stderr, "failed to create '{}'\n",
                           assembly.string());
            }
            else
            {
                out << ".text\n.globl _jit_function\n_jit_function:\n";
                for(uint32_t word: words)
                {
                    out << ".long 0x" << std::hex << word << "\n";
                }
                if(!pool_values.empty())
                {
                    out << ".space " << std::dec << pool_offset - code_size
                        << "\n";
                    for(size_t index = 0; index < pool_values.size(); ++index)
                    {
                        out << ".globl _constant_pool_" << std::dec << index
                            << "\n_constant_pool_" << index << ":\n.quad 0x"
                            << std::hex
                            << static_cast<uint64_t>(
                                   pool_values[index].as.integer)
                            << "\n";
                    }
                }
                out.close();

#if defined(__APPLE__)
                const char *target = "arm64-apple-macos";
#else
                const char *target = "aarch64-unknown-linux-gnu";
#endif
                std::vector<std::string> clang_arguments = {command_line.clang,
                                                            "-target",
                                                            target,
                                                            "-x",
                                                            "assembler",
                                                            "-c",
                                                            assembly.string(),
                                                            "-o",
                                                            object.string()};
                if(run_process(clang_arguments) == 0)
                {
                    std::fflush(stdout);
                    std::vector<std::string> objdump_arguments = {
                        command_line.objdump, "-d", "--no-show-raw-insn",
                        "--disassemble-symbols=_jit_function", object.string()};
                    success = run_process(objdump_arguments) == 0;
                }
            }
        }

        std::error_code ignored;
        std::filesystem::remove_all(*temporary, ignored);
        return success;
    }

    class DumpObserver : public cl::jit::JitCompilationObserver
    {
    public:
        explicit DumpObserver(const CommandLine &command_line)
            : command_line_(&command_line)
        {
        }

        void on_bytecode(const cl::CodeObject &code_object) override
        {
            fmt::print("Bytecode:\n{}\n", code_object);
        }

        void
        on_core_ir_translated(const cl::jit::ControlFlowGraph &graph) override
        {
            fmt::print("Core IR:\n{}\n", cl::jit::format_ir(graph));
        }

        void
        on_core_ir_optimized(const cl::jit::ControlFlowGraph &graph) override
        {
            fmt::print("Optimized Core IR:\n{}\n", cl::jit::format_ir(graph));
        }

        void on_machine_ir(const cl::jit::ControlFlowGraph &graph) override
        {
            fmt::print("Machine IR:\n{}\n", cl::jit::format_ir(graph));
        }

        void on_machine_code(const cl::jit::PublishedCode &code) override
        {
            std::vector<uint32_t> words = machine_words(code);
            if(words.empty())
            {
                fmt::print(stderr,
                           "AArch64 backend emitted malformed machine code\n");
                succeeded_ = false;
                return;
            }

            int64_t signed_pool_offset =
                code.entry().displacement_to(code.constant_pool_address());
            if(signed_pool_offset < 0)
            {
                fmt::print(stderr,
                           "AArch64 constant pool precedes generated code\n");
                succeeded_ = false;
                return;
            }
            size_t pool_offset = static_cast<size_t>(signed_pool_offset);
            std::span<const cl::Value> pool_values = code.tagged_values();
            if(!pool_values.empty())
            {
                fmt::print("AArch64 constant pool:\n");
                for(size_t index = 0; index < pool_values.size(); ++index)
                {
                    fmt::print(
                        "  +0x{:x} <_constant_pool_{}>: 0x{:016x}\n",
                        pool_offset + index * sizeof(cl::Value), index,
                        static_cast<uint64_t>(pool_values[index].as.integer));
                }
                fmt::print("\n");
            }
            fmt::print("AArch64 disassembly:\n");
            succeeded_ = print_disassembly(words, pool_values, pool_offset,
                                           *command_line_);
        }

        bool succeeded() const { return succeeded_; }

    private:
        const CommandLine *command_line_;
        bool succeeded_ = true;
    };
}  // namespace

int main(int argc, const char *argv[])
{
    cxxopts::Options options("clovervm-jit-dump",
                             "Inspect CloverVM's AArch64 JIT pipeline");
    CommandLine command_line;
    try
    {
        command_line = parse_command_line(argc, argv, options);
    }
    catch(const cxxopts::exceptions::exception &error)
    {
        fmt::print(stderr,
                   "clovervm-jit-dump: {}\n"
                   "Try 'clovervm-jit-dump --help' for more information.\n",
                   error.what());
        return 1;
    }
    if(command_line.show_help)
    {
        fmt::print("{}\n", options.help());
        return 0;
    }

    std::optional<SourceInput> input = read_source(command_line);
    if(!input.has_value())
    {
        fmt::print(stderr, "failed to open or decode Python source\n");
        return 1;
    }

    cl::VirtualMachine vm;
    cl::ThreadState *thread = vm.get_default_thread();
    cl::ThreadState::ActivationScope activation_scope(thread);
    cl::Expected<cl::CodeObject *> compilation =
        input->filename.has_value()
            ? thread->compile(input->source.c_str(), cl::StartRule::File,
                              input->filename->c_str())
            : thread->compile(input->source.c_str(), cl::StartRule::File);
    if(compilation.has_exception())
    {
        std::wcerr << format_pending_python_exception(thread) << L"\n";
        return 1;
    }

    cl::CodeObject *function = single_function_code(*compilation.value());
    if(function == nullptr)
    {
        fmt::print(
            stderr,
            "Python source must define exactly one top-level function\n");
        return 1;
    }

    fmt::print("Python:\n{}\n\n", cl::unicode::encode_utf8(input->source));
    DumpObserver observer(command_line);
    cl::jit::JitCompilerOptions compiler_options{&observer};
    auto code_result =
        cl::jit::compile_jit_code(*thread, *function, compiler_options);
    if(!code_result)
    {
        fmt::print(stderr, "JIT compilation failed: {}\n",
                   jit_compilation_error_name(code_result.error()));
        return 1;
    }
    return observer.succeeded() ? 0 : 1;
}
