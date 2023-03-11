#include <fmt/core.h>

#include "object.h"
#include "value.h"
#include "short_vector.h"
#include "string.h"
#include "refcount.h"
#include <sstream>
#include <fstream>
#include <codecvt>
#include "virtual_machine.h"
#include "thread_state.h"
#include "parser.h"
#include "code_object_print.h"
#include <fmt/xchar.h>

using namespace cl;


std::wstring read_file(const char* filename)
{
    std::wifstream wif(filename);
    wif.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
    std::wstringstream wss;
    wss << wif.rdbuf();
    return wss.str();
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
        } else {
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
        CodeObject code_obj = vm.get_default_thread()->compile(file_contents.c_str(), StartRule::File);
        if(print_bytecode)
        {
            std::cout << fmt::to_string(code_obj) << "\n";
        }
        Value v = vm.get_default_thread()->run(&code_obj);
        std::cout << v.get_smi() << "\n";
    }

    return 0;
}
