#include <clovervm/clovervm.h>

#include "code_object.h"
#include "code_object_print.h"
#include "exception_object.h"
#include "parser.h"
#include "repl.h"
#include "source_text.h"
#include "str.h"
#include "thread_state.h"
#include "typed_value.h"
#include "unicode.h"
#include "value.h"
#include "virtual_machine.h"
#include <fmt/core.h>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

struct clover_vm
{
    cl::VirtualMachine vm;
};

namespace
{
    std::wstring decode_api_string(const char *str, const char *error_message)
    {
        std::optional<std::wstring> result =
            cl::unicode::decode_utf8_c_string(str);
        if(!result.has_value())
        {
            throw std::runtime_error(error_message);
        }
        return *result;
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

    clover_status run_code_object(cl::ThreadState *thread, cl::CodeObject *code,
                                  bool print_bytecode)
    {
        if(print_bytecode)
        {
            fmt::print("{}\n", *code);
        }

        cl::Value result = thread->run_clovervm_code_object(code);
        if(result.is_exception_marker())
        {
            std::wcerr << format_pending_python_exception(thread) << L"\n";
            return CLOVER_STATUS_ERROR;
        }
        return CLOVER_STATUS_OK;
    }

    clover_status run_file_impl(clover_vm *api_vm, const char *path,
                                bool print_bytecode)
    {
        if(api_vm == nullptr)
        {
            std::cerr << "clover_vm_run_file called with null vm\n";
            return CLOVER_STATUS_ERROR;
        }

        std::wstring filename =
            decode_api_string(path, "failed to decode source filename");
        std::optional<std::wstring> file_contents =
            cl::read_source_text_file(filename);
        if(!file_contents.has_value())
        {
            std::cerr << "failed to open or decode source file '" << path
                      << "'\n";
            return CLOVER_STATUS_ERROR;
        }

        cl::ThreadState *thread = api_vm->vm.get_default_thread();
        cl::Expected<cl::CodeObject *> code = thread->compile(
            file_contents->c_str(), cl::StartRule::File, filename.c_str());
        if(code.has_exception())
        {
            std::wcerr << format_pending_python_exception(thread) << L"\n";
            return CLOVER_STATUS_ERROR;
        }
        return run_code_object(thread, code.value(), print_bytecode);
    }

    clover_status run_string_impl(clover_vm *api_vm, const char *source,
                                  bool print_bytecode)
    {
        if(api_vm == nullptr)
        {
            std::cerr << "clover_vm_run_string called with null vm\n";
            return CLOVER_STATUS_ERROR;
        }

        std::wstring source_text =
            decode_api_string(source, "failed to decode source string");
        cl::ThreadState *thread = api_vm->vm.get_default_thread();
        cl::Expected<cl::CodeObject *> code =
            thread->compile(source_text.c_str(), cl::StartRule::File);
        if(code.has_exception())
        {
            std::wcerr << format_pending_python_exception(thread) << L"\n";
            return CLOVER_STATUS_ERROR;
        }
        return run_code_object(thread, code.value(), print_bytecode);
    }
}  // namespace

extern "C"
{

    CL_EXPORT clover_vm *clover_vm_new(void)
    {
        try
        {
            return new clover_vm();
        }
        catch(const std::bad_alloc &)
        {
            return nullptr;
        }
        catch(const std::exception &err)
        {
            std::cerr << err.what() << "\n";
            return nullptr;
        }
    }

    CL_EXPORT void clover_vm_destroy(clover_vm *vm) { delete vm; }

    CL_EXPORT void clover_vm_set_trace_instructions(clover_vm *vm, int enabled)
    {
        if(vm == nullptr)
        {
            return;
        }
        vm->vm.get_default_thread()->set_trace_interpreter_instructions(
            enabled != 0);
    }

    CL_EXPORT clover_status clover_vm_run_file(clover_vm *vm, const char *path,
                                               int print_bytecode)
    {
        try
        {
            return run_file_impl(vm, path, print_bytecode != 0);
        }
        catch(const std::exception &err)
        {
            std::cerr << err.what() << "\n";
            return CLOVER_STATUS_ERROR;
        }
    }

    CL_EXPORT clover_status clover_vm_run_string(clover_vm *vm,
                                                 const char *source,
                                                 int print_bytecode)
    {
        try
        {
            return run_string_impl(vm, source, print_bytecode != 0);
        }
        catch(const std::exception &err)
        {
            std::cerr << err.what() << "\n";
            return CLOVER_STATUS_ERROR;
        }
    }

    CL_EXPORT clover_status clover_vm_run_interactive(clover_vm *vm,
                                                      int print_bytecode)
    {
        if(vm == nullptr)
        {
            std::cerr << "clover_vm_run_interactive called with null vm\n";
            return CLOVER_STATUS_ERROR;
        }

        try
        {
            return cl::run_repl(vm->vm, print_bytecode != 0) == 0
                       ? CLOVER_STATUS_OK
                       : CLOVER_STATUS_ERROR;
        }
        catch(const std::exception &err)
        {
            std::cerr << err.what() << "\n";
            return CLOVER_STATUS_ERROR;
        }
    }

}  // extern "C"
