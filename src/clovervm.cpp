#include <fmt/core.h>

#include "class_object.h"
#include "code_object_print.h"
#include "exception_object.h"
#include "object.h"
#include "parser.h"
#include "refcount.h"
#include "repl.h"
#include "str.h"
#include "string.h"
#include "thread_state.h"
#include "value.h"
#include "virtual_machine.h"
#include <cerrno>
#include <clocale>
#include <cwchar>
#include <fmt/xchar.h>
#include <fstream>
#include <iostream>
#include <locale>
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

std::wstring cl_string_to_wstring(TValue<String> string)
{
    String *str = string.extract();
    return std::wstring(str->data, size_t(str->count.extract()));
}

std::wstring format_pending_python_exception(ThreadState *thread)
{
    if(thread->pending_exception_kind() == PendingExceptionKind::StopIteration)
    {
        return L"StopIteration";
    }

    if(thread->pending_exception_kind() != PendingExceptionKind::Object)
    {
        return L"InternalError: exception marker without pending exception";
    }

    TValue<ExceptionObject> exception =
        TValue<ExceptionObject>::from_value_checked(
            thread->pending_exception_object());
    std::wstring result = cl_string_to_wstring(
        exception.extract()->get_shape()->get_class()->get_name());
    std::wstring message = cl_string_to_wstring(
        static_cast<TValue<String>>(exception.extract()->message));
    if(!message.empty())
    {
        result += L": ";
        result += message;
    }
    return result;
}

int main(int argc, const char *argv[])
{
    std::setlocale(LC_CTYPE, "");
    std::locale::global(std::locale(""));
    std::wcout.imbue(std::locale());
    std::wcerr.imbue(std::locale());

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

    if(source_file == nullptr)
    {
        return run_repl(print_bytecode);
    }
    else
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
        Value v = thr->run_clovervm_code_object(code_obj);
        if(v.is_exception_marker())
        {
            std::wcerr << format_pending_python_exception(thr) << L"\n";
            return 1;
        }
        else if(v.is_smi())
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
