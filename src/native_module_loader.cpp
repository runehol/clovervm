#include "native_module_loader.h"

#include <clovervm/native_module.h>

#include "module_finder.h"
#include "module_object.h"
#include "native_module_loader_internal.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <cerrno>
#include <cwchar>
#include <optional>
#include <string>

struct clover_native_module_builder
{
    cl::ThreadState *thread;
    cl::ModuleObject *module;
};

namespace cl
{
    namespace
    {
        constexpr int64_t kMinSmi = -(int64_t{1} << 58);
        constexpr int64_t kMaxSmi = (int64_t{1} << 58) - 1;

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

        clover_status
        set_builder_import_error(clover_native_module_builder *builder,
                                 const std::wstring &message)
        {
            if(builder != nullptr && builder->thread != nullptr)
            {
                set_native_import_error(builder->thread, message);
            }
            return CLOVER_STATUS_ERROR;
        }

        std::optional<std::wstring> decode_native_api_string(const char *str)
        {
            if(str == nullptr)
            {
                return std::nullopt;
            }

            const char *src = str;
            std::mbstate_t state = std::mbstate_t();
            errno = 0;
            size_t size = std::mbsrtowcs(nullptr, &src, 0, &state);
            if(size == static_cast<size_t>(-1))
            {
                return std::nullopt;
            }

            std::wstring result(size, L'\0');
            src = str;
            state = std::mbstate_t();
            errno = 0;
            if(std::mbsrtowcs(result.data(), &src, result.size(), &state) ==
               static_cast<size_t>(-1))
            {
                return std::nullopt;
            }
            return result;
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
    }  // namespace

    extern "C" CL_EXPORT clover_status clover_module_add_int_constant(
        clover_native_module_builder *builder, const char *name, int64_t value)
    {
        if(builder == nullptr || builder->thread == nullptr ||
           builder->module == nullptr)
        {
            return CLOVER_STATUS_ERROR;
        }

        std::optional<std::wstring> decoded_name =
            decode_native_api_string(name);
        if(!decoded_name.has_value() || decoded_name->empty())
        {
            return set_builder_import_error(
                builder, L"native module int constant name must be non-empty");
        }

        if(value < kMinSmi || value > kMaxSmi)
        {
            return set_builder_import_error(
                builder,
                L"native module int constant is outside the supported range");
        }

        bool stored = builder->module->set_own_property(
            interned_string(builder->thread, *decoded_name),
            Value::from_smi(value));
        if(!stored)
        {
            return set_builder_import_error(
                builder, L"native module could not set int constant");
        }
        return CLOVER_STATUS_OK;
    }

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

        auto init_function =
            reinterpret_cast<clover_status (*)(clover_native_module_builder *)>(
                init_symbol);
        clover_native_module_builder builder{thread, module};
        clover_status status = init_function(&builder);
        if(status == CLOVER_STATUS_OK)
        {
            return Value::None();
        }
        if(thread->pending_exception_kind() == PendingExceptionKind::None)
        {
            return set_native_init_failed_without_exception(thread, spec);
        }
        return Value::exception_marker();
    }

}  // namespace cl
