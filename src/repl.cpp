#include "repl.h"

#include "code_object.h"
#include "code_object_print.h"
#include "codegen.h"
#include "exception_object.h"
#include "module_object.h"
#include "parser.h"
#include "scope.h"
#include "str.h"
#include "thread_state.h"
#include "typed_value.h"
#include "unicode.h"
#include "value.h"
#include "value_string.h"
#include "virtual_machine.h"
#include <algorithm>
#include <fmt/core.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace cl
{
    static constexpr uint32_t repl_indent_width = 4;

    static std::optional<std::wstring>
    decode_stdin_line(const std::string &bytes)
    {
        return unicode::decode_utf8(bytes);
    }

    static std::wstring cl_string_to_wstring(TValue<String> string)
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

        TValue<Exception> exception = thread->pending_exception_object();
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
                          TValue<String>::from_value_checked(repr).value())
                   << L"\n";
    }

    static bool is_blank_line(const std::wstring &line)
    {
        return std::all_of(line.begin(), line.end(), [](wchar_t c) {
            return c == L' ' || c == L'\t' || c == L'\f' || c == L'\r';
        });
    }

    int run_repl(VirtualMachine &vm, bool print_bytecode)
    {
        ThreadState *thr = vm.get_default_thread();
        ThreadState::ActivationScope active_thread(thr);
        ModuleObject *module = thr->make_main_module(Value::not_present());

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

            std::optional<std::wstring> source = decode_stdin_line(line);
            if(!source.has_value())
            {
                std::cerr << "failed to decode stdin\n";
                continue;
            }
            bool blank_line = is_blank_line(*source);
            if(source_buffer.empty() && blank_line)
            {
                continue;
            }
            source_buffer += *source;
            source_buffer += L"\n";

            if(!blank_line && suite_waiting_for_blank_line)
            {
                CompileContinuationInfo compile_continuation_info;
                try
                {
                    Expected<CodeObject *> code_obj = thr->compile_in_module(
                        source_buffer.c_str(), StartRule::Interactive, module,
                        LanguageMode::StandardsCompliant,
                        &compile_continuation_info);
                    if(code_obj.has_exception())
                    {
                        if(compile_continuation_info.incomplete_input)
                        {
                            thr->clear_pending_exception();
                            continuation_indentation =
                                compile_continuation_info
                                    .next_indentation_level *
                                repl_indent_width;
                            continue;
                        }
                        print_pending_exception_and_clear(thr);
                        source_buffer.clear();
                        suite_waiting_for_blank_line = false;
                        continuation_indentation = 0;
                        continue;
                    }
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

            CompileContinuationInfo compile_continuation_info;
            try
            {
                Expected<CodeObject *> code_obj = thr->compile_in_module(
                    source_buffer.c_str(), StartRule::Interactive, module,
                    LanguageMode::StandardsCompliant,
                    &compile_continuation_info);
                if(code_obj.has_exception())
                {
                    if(!blank_line &&
                       compile_continuation_info.incomplete_input)
                    {
                        thr->clear_pending_exception();
                        continuation_indentation =
                            compile_continuation_info.next_indentation_level *
                            repl_indent_width;
                        suite_waiting_for_blank_line =
                            compile_continuation_info.next_indentation_level >
                            prompt_indentation_level;
                        continue;
                    }
                    print_pending_exception_and_clear(thr);
                    source_buffer.clear();
                    suite_waiting_for_blank_line = false;
                    continuation_indentation = 0;
                    continue;
                }
                source_buffer.clear();
                suite_waiting_for_blank_line = false;
                continuation_indentation = 0;
                if(print_bytecode)
                {
                    fmt::print("{}\n", *code_obj.value());
                }

                Value result = thr->run_clovervm_code_object(code_obj.value());
                if(result.is_exception_marker())
                {
                    print_pending_exception_and_clear(thr);
                    continue;
                }

                print_value_repr(result, thr);
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
