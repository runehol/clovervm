#include "repl.h"

#include "code_object.h"
#include "code_object_print.h"
#include "codegen.h"
#include "exception_object.h"
#include "parser.h"
#include "scope.h"
#include "str.h"
#include "thread_state.h"
#include "typed_value.h"
#include "value.h"
#include "value_string.h"
#include "virtual_machine.h"
#include <algorithm>
#include <cerrno>
#include <cwchar>
#include <fmt/core.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace cl
{
    static constexpr uint32_t repl_indent_width = 4;

    static std::wstring decode_stdin_line(const std::string &bytes)
    {
        const char *src = bytes.c_str();
        std::mbstate_t state = std::mbstate_t();
        errno = 0;
        size_t size = std::mbsrtowcs(nullptr, &src, 0, &state);
        if(size == static_cast<size_t>(-1))
        {
            throw std::runtime_error("failed to decode stdin");
        }

        std::wstring result(size, L'\0');
        src = bytes.c_str();
        state = std::mbstate_t();
        errno = 0;
        if(std::mbsrtowcs(result.data(), &src, result.size(), &state) ==
           static_cast<size_t>(-1))
        {
            throw std::runtime_error("failed to decode stdin");
        }
        return result;
    }

    static std::wstring cl_string_to_wstring(TValue2<String> string)
    {
        String *str = string.extract();
        return std::wstring(str->data, size_t(str->count.extract()));
    }

    static std::wstring format_pending_python_exception(ThreadState *thread)
    {
        if(thread->pending_exception_kind() ==
           PendingExceptionKind::StopIteration)
        {
            return L"StopIteration";
        }

        if(thread->pending_exception_kind() != PendingExceptionKind::Object)
        {
            return L"InternalError: exception marker without pending exception";
        }

        TValue2<Exception> exception = thread->pending_exception_object();
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

    static void print_pending_exception_and_clear(ThreadState *thread)
    {
        std::wcerr << format_pending_python_exception(thread) << L"\n";
        thread->clear_pending_exception();
    }

    static void print_value_repr(Value value, ThreadState *thread)
    {
        if(value == Value::None())
        {
            return;
        }

        Value repr = value_to_repr_string(value);
        if(repr.is_exception_marker())
        {
            print_pending_exception_and_clear(thread);
            return;
        }

        std::wcout << cl_string_to_wstring(
                          TValue2<String>::from_value_checked(repr).value())
                   << L"\n";
    }

    static bool is_blank_line(const std::wstring &line)
    {
        return std::all_of(line.begin(), line.end(), [](wchar_t c) {
            return c == L' ' || c == L'\t' || c == L'\f' || c == L'\r';
        });
    }

    int run_repl(bool print_bytecode)
    {
        VirtualMachine vm;
        ThreadState *thr = vm.get_default_thread();
        ThreadState::ActivationScope active_thread(thr);
        Scope *module_scope =
            thr->make_internal_raw<Scope>(vm.builtin_scope_ptr());

        std::wstring source_buffer;
        bool suite_waiting_for_blank_line = false;
        uint32_t continuation_indentation = 0;
        std::string line;
        while(true)
        {
            uint32_t prompt_indentation_level =
                continuation_indentation / repl_indent_width;
            std::cout << (source_buffer.empty() ? ">>> " : "... ")
                      << std::flush;
            if(!std::getline(std::cin, line))
            {
                std::cout << "\n";
                return 0;
            }

            std::wstring source;
            try
            {
                source = decode_stdin_line(line);
            }
            catch(const std::runtime_error &err)
            {
                std::cerr << err.what() << "\n";
                continue;
            }
            bool blank_line = is_blank_line(source);
            if(source_buffer.empty() && blank_line)
            {
                continue;
            }
            source_buffer += source;
            source_buffer += L"\n";

            if(!blank_line && suite_waiting_for_blank_line)
            {
                try
                {
                    (void)thr->compile_in_scope(
                        source_buffer.c_str(), StartRule::Interactive,
                        L"<stdin>", module_scope,
                        LanguageMode::StandardsCompliant);
                }
                catch(const ParseError &err)
                {
                    if(err.incomplete_input())
                    {
                        continuation_indentation =
                            err.next_indentation_level() * repl_indent_width;
                        continue;
                    }
                    std::cerr << err.what() << "\n";
                    source_buffer.clear();
                    suite_waiting_for_blank_line = false;
                    continuation_indentation = 0;
                    continue;
                }
                catch(const std::runtime_error &err)
                {
                    std::cerr << err.what() << "\n";
                    source_buffer.clear();
                    suite_waiting_for_blank_line = false;
                    continuation_indentation = 0;
                    continue;
                }
                continue;
            }

            try
            {
                CodeObject *code_obj = thr->compile_in_scope(
                    source_buffer.c_str(), StartRule::Interactive, L"<stdin>",
                    module_scope, LanguageMode::StandardsCompliant);
                source_buffer.clear();
                suite_waiting_for_blank_line = false;
                continuation_indentation = 0;
                if(print_bytecode)
                {
                    fmt::print("{}\n", *code_obj);
                }

                Value result = thr->run_clovervm_code_object(code_obj);
                if(result.is_exception_marker())
                {
                    print_pending_exception_and_clear(thr);
                    continue;
                }

                print_value_repr(result, thr);
            }
            catch(const ParseError &err)
            {
                if(!blank_line && err.incomplete_input())
                {
                    continuation_indentation =
                        err.next_indentation_level() * repl_indent_width;
                    suite_waiting_for_blank_line =
                        err.next_indentation_level() > prompt_indentation_level;
                    continue;
                }
                std::cerr << err.what() << "\n";
                source_buffer.clear();
                suite_waiting_for_blank_line = false;
                continuation_indentation = 0;
            }
            catch(const std::runtime_error &err)
            {
                std::cerr << err.what() << "\n";
                source_buffer.clear();
                suite_waiting_for_blank_line = false;
                continuation_indentation = 0;
            }
        }
    }

}  // namespace cl
