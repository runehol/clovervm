#include "native/native_module_loader.h"

#include <clovervm/native_module.h>

#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "import_system/module_finder.h"
#include "native/native_module_api_internal.h"
#include "native/native_module_loader_internal.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <string>

namespace cl
{
    namespace
    {
        TValue<String> interned_string(ThreadState *thread,
                                       const std::wstring &text)
        {
            return thread->get_machine()->get_or_create_interned_string_value(
                text);
        }

        Value set_native_import_error(ThreadState *thread,
                                      const std::wstring &message)
        {
            return thread->set_pending_builtin_exception_string(
                L"ImportError", interned_string(thread, message));
        }

        Value native_init_symbol_name(ThreadState *thread,
                                      const ModuleSpec &spec,
                                      std::string &symbol_name)
        {
            symbol_name = "clover_module_init_";
            for(wchar_t ch: spec.name)
            {
                if(ch == L'.')
                {
                    symbol_name += '_';
                    continue;
                }
                if(ch < 0 || ch > 0x7f)
                {
                    std::wstring message =
                        L"native module name must be ASCII: '";
                    message += spec.name;
                    message += L"'";
                    return set_native_import_error(thread, message);
                }
                symbol_name += static_cast<char>(ch);
            }
            return Value::None();
        }

        void append_ascii(std::wstring &target, const char *text)
        {
            while(*text != '\0')
            {
                target += static_cast<unsigned char>(*text);
                ++text;
            }
        }

        Value native_library_error(ThreadState *thread,
                                   const std::wstring &prefix,
                                   const std::wstring &module_name)
        {
            std::wstring message = prefix;
            message += L"'";
            message += module_name;
            message += L"': ";
            const char *detail = native_library_last_error();
            if(detail == nullptr)
            {
                message += L"unknown dynamic loader error";
            }
            else
            {
                append_ascii(message, detail);
            }
            return set_native_import_error(thread, message);
        }

        NativeLibraryHandle *get_or_open_native_library(ThreadState *thread,
                                                        const ModuleSpec &spec)
        {
            NativeLibraryHandleCache &cache =
                thread->get_machine()->native_library_handle_cache();
            std::lock_guard<std::mutex> lock(cache.mutex);

            auto found = cache.handles.find(spec.origin);
            if(found != cache.handles.end())
            {
                return found->second.get();
            }

            std::unique_ptr<NativeLibraryHandle> handle =
                std::make_unique<NativeLibraryHandle>();
            handle->platform_handle = native_library_open(spec.origin);
            if(handle->platform_handle == nullptr)
            {
                native_library_error(thread, L"cannot open native module ",
                                     spec.name);
                return nullptr;
            }

            NativeLibraryHandle *result = handle.get();
            cache.handles.emplace(spec.origin, std::move(handle));
            return result;
        }

        Value set_native_init_failed_without_exception(ThreadState *thread,
                                                       const ModuleSpec &spec)
        {
            std::wstring message = L"native module init failed without "
                                   L"setting an exception for '";
            message += spec.name;
            message += L"'";
            return set_native_import_error(thread, message);
        }

        Value set_native_init_succeeded_with_exception(ThreadState *thread,
                                                       const ModuleSpec &spec)
        {
            std::wstring message = L"native module init returned success with "
                                   L"an exception set for '";
            message += spec.name;
            message += L"'";
            return thread->set_pending_builtin_exception_string(
                L"SystemError", interned_string(thread, message));
        }
    }  // namespace

    Value exec_native_extension_module(ThreadState *thread,
                                       const ModuleSpec &spec,
                                       ModuleObject *module)
    {
        std::string symbol_name;
        Value symbol_result =
            native_init_symbol_name(thread, spec, symbol_name);
        if(symbol_result.is_exception_marker())
        {
            return symbol_result;
        }

        NativeLibraryHandle *library = get_or_open_native_library(thread, spec);
        if(library == nullptr)
        {
            return Value::exception_marker();
        }

        void *init_symbol = native_library_symbol(library->platform_handle,
                                                  symbol_name.c_str());
        if(init_symbol == nullptr)
        {
            std::wstring message = L"native module '";
            message += spec.name;
            message += L"' does not export '";
            for(char ch: symbol_name)
            {
                message += static_cast<unsigned char>(ch);
            }
            message += L"'";
            return set_native_import_error(thread, message);
        }

        auto init_function = reinterpret_cast<clover_status (*)(
            clover_context *, clover_native_module_builder *)>(init_symbol);
        clover_context ctx{thread};
        clover_native_module_builder builder{thread, module};
        clover_status status = init_function(&ctx, &builder);
        if(status == CLOVER_STATUS_OK)
        {
            if(thread->has_pending_exception())
            {
                return set_native_init_succeeded_with_exception(thread, spec);
            }
            return Value::None();
        }
        if(thread->pending_exception_kind() == PendingExceptionKind::None)
        {
            return set_native_init_failed_without_exception(thread, spec);
        }
        return Value::exception_marker();
    }

}  // namespace cl
