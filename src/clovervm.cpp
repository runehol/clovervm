#include <fmt/core.h>

#include "code_object_print.h"
#include "object.h"
#include "parser.h"
#include "refcount.h"
#include "string.h"
#include "thread_state.h"
#include "value.h"
#include "virtual_machine.h"
#include <cerrno>
#include <cwchar>
#include <fmt/xchar.h>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace cl;

std::wstring decode_string(const std::string &bytes, const char *error_message)
{
    const char *src = bytes.c_str();
    std::mbstate_t state = std::mbstate_t();
    errno = 0;
    size_t size = std::mbsrtowcs(nullptr, &src, 0, &state);
    if(size == static_cast<size_t>(-1))
    {
        throw std::runtime_error(error_message);
    }

    std::wstring result(size, L'\0');
    src = bytes.c_str();
    state = std::mbstate_t();
    errno = 0;
    if(std::mbsrtowcs(result.data(), &src, result.size(), &state) ==
       static_cast<size_t>(-1))
    {
        throw std::runtime_error(error_message);
    }
    return result;
}

std::wstring read_file(const char *filename)
{
    std::ifstream ifs(filename, std::ios::binary);
    if(!ifs)
    {
        throw std::runtime_error(
            fmt::format("failed to open source file '{}'", filename));
    }

    std::string bytes((std::istreambuf_iterator<char>(ifs)),
                      std::istreambuf_iterator<char>());
    return decode_string(bytes, "failed to decode source file");
}

std::wstring widen_string(const char *str)
{
    return decode_string(str, "failed to decode string");
}

int main(int argc, const char *argv[])
{
    using namespace std::literals;
    bool print_bytecode = false;
    const char *source_file = nullptr;
    int i = 1;
    for(; i < argc; ++i)
    {
        if(argv[i] == "--print-bytecode"sv)
        {
            print_bytecode = true;
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

    // read the file and execute it

    if(source_file != nullptr)
    {
        VirtualMachine vm;
        std::wstring file_contents = read_file(source_file);
        std::wstring module_name = widen_string(source_file);
        ThreadState *thr = vm.get_default_thread();
        CodeObject *code_obj = thr->compile(
            file_contents.c_str(), StartRule::File, module_name.c_str());
        if(print_bytecode)
        {
            fmt::print("{}\n", *code_obj);
        }
        Value v = thr->run(code_obj);
        if(v.is_smi())
        {
            std::cout << v.get_smi() << "\n";
        }
        else if(v == Value::None())
        {
            std::cout << "None" << "\n";
        }
        else if(v == Value::False())
        {
            std::cout << "False" << "\n";
        }
        else if(v == Value::True())
        {
            std::cout << "True" << "\n";
        }
        else
        {
            std::cout << "unknown object\n";
        }
    }

    return 0;
}
