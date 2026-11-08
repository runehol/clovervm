#include "heap_runner.h"

#include <cxxopts.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    namespace differential = cl::gc::test::differential;
    namespace format = cl::gc::test::format;

    struct CommandLine
    {
        differential::Algorithm algorithm;
        std::filesystem::path heap_file;
    };

    CommandLine parse_command_line(const cxxopts::ParseResult &parsed)
    {
        if(parsed.count("algorithm") != 1)
            throw std::runtime_error(
                "specify exactly one --algorithm mark|evacuate");
        if(parsed.count("collection") != 1)
            throw std::runtime_error("specify exactly one --collection full");
        if(parsed["collection"].as<std::string>() != "full")
            throw std::runtime_error(
                "unsupported collection; only full is supported");
        const auto algorithm = parsed["algorithm"].as<std::string>();
        if(algorithm != "mark" && algorithm != "evacuate")
            throw std::runtime_error(
                "unsupported algorithm; use mark or evacuate");
        // Non-option arguments are preserved verbatim, including commas in
        // paths. Unknown options are rejected by cxxopts itself.
        if(parsed.unmatched().size() != 1)
            throw std::runtime_error("expected exactly one input heap file");
        return {algorithm == "mark" ? differential::Algorithm::Mark
                                    : differential::Algorithm::Evacuate,
                parsed.unmatched().front()};
    }

    std::string read_file(const std::filesystem::path &path)
    {
        if(std::filesystem::is_directory(path))
            throw std::runtime_error("cannot read input file: " +
                                     path.string());
        std::ifstream file(path, std::ios::binary);
        if(!file)
            throw std::runtime_error("cannot open input file: " +
                                     path.string());
        std::string contents;
        std::array<char, 8192> buffer;
        while(file)
        {
            file.read(buffer.data(), buffer.size());
            contents.append(buffer.data(), static_cast<size_t>(file.gcount()));
        }
        if(!file.eof())
            throw std::runtime_error("cannot read input file: " +
                                     path.string());
        return contents;
    }

    void write_stdout(const std::string &output)
    {
        if(std::fwrite(output.data(), 1, output.size(), stdout) !=
               output.size() ||
           std::fflush(stdout) != 0)
            throw std::runtime_error("cannot write stdout");
    }
}  // namespace

int main(int argc, const char *argv[])
{
    try
    {
        cxxopts::Options options("gc_cpp",
                                 "Collect a GC heap JSON v1 snapshot.");
        options.custom_help("--algorithm mark|evacuate --collection full FILE");
        options.add_options()("h,help", "Print help and exit")(
            "algorithm", "Collection algorithm: mark or evacuate",
            cxxopts::value<std::string>())("collection",
                                           "Collection selection: full",
                                           cxxopts::value<std::string>());
        const auto parsed = options.parse(argc, argv);
        if(parsed.count("help") != 0)
        {
            write_stdout(options.help() + "\n");
            return 0;
        }
        const auto command = parse_command_line(parsed);
        const auto input = format::parse_heap(read_file(command.heap_file));
        const auto result =
            differential::run_collection(input, command.algorithm);
        // Validate and serialize the complete snapshot before emitting heap
        // data.
        const auto output = format::write_heap(result);
        write_stdout(output);
        return 0;
    }
    catch(const std::exception &error)
    {
        std::cerr << "gc_cpp: " << error.what() << '\n';
        return 1;
    }
}
