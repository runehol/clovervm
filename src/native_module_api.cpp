#include "native_module_api_internal.h"

#include "module_object.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <cerrno>
#include <cwchar>
#include <optional>

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

        clover_status
        set_builder_import_error(clover_native_module_builder *builder,
                                 const std::wstring &message)
        {
            if(builder != nullptr && builder->thread != nullptr)
            {
                (void)builder->thread->set_pending_builtin_exception_string(
                    L"ImportError", interned_string(builder->thread, message));
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
    }  // namespace
}  // namespace cl

extern "C" CL_EXPORT clover_status clover_module_add_int_constant(
    clover_native_module_builder *builder, const char *name, int64_t value)
{
    if(builder == nullptr || builder->thread == nullptr ||
       builder->module == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    std::optional<std::wstring> decoded_name =
        cl::decode_native_api_string(name);
    if(!decoded_name.has_value() || decoded_name->empty())
    {
        return cl::set_builder_import_error(
            builder, L"native module int constant name must be non-empty");
    }

    if(value < cl::kMinSmi || value > cl::kMaxSmi)
    {
        return cl::set_builder_import_error(
            builder,
            L"native module int constant is outside the supported range");
    }

    bool stored = builder->module->set_own_property(
        cl::interned_string(builder->thread, *decoded_name),
        cl::Value::from_smi(value));
    if(!stored)
    {
        return cl::set_builder_import_error(
            builder, L"native module could not set int constant");
    }
    return CLOVER_STATUS_OK;
}
