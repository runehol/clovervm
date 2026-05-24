#include "native_module_api_internal.h"

#include "module_object.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <cstdint>
#include <optional>
#include <string>

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

        bool is_continuation_byte(unsigned char byte)
        {
            return (byte & 0xc0) == 0x80;
        }

        void append_codepoint(std::wstring &result, uint32_t codepoint)
        {
            if constexpr(sizeof(wchar_t) == 2)
            {
                if(codepoint > 0xffff)
                {
                    codepoint -= 0x10000;
                    result.push_back(
                        static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
                    result.push_back(
                        static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff)));
                    return;
                }
            }
            result.push_back(static_cast<wchar_t>(codepoint));
        }

        std::optional<std::wstring> decode_utf8_api_string(const char *str)
        {
            if(str == nullptr)
            {
                return std::nullopt;
            }

            std::wstring result;
            const unsigned char *src =
                reinterpret_cast<const unsigned char *>(str);
            while(*src != '\0')
            {
                uint32_t codepoint = 0;
                size_t continuation_count = 0;
                unsigned char first = *src++;
                if(first < 0x80)
                {
                    codepoint = first;
                }
                else if((first & 0xe0) == 0xc0)
                {
                    codepoint = first & 0x1f;
                    continuation_count = 1;
                }
                else if((first & 0xf0) == 0xe0)
                {
                    codepoint = first & 0x0f;
                    continuation_count = 2;
                }
                else if((first & 0xf8) == 0xf0)
                {
                    codepoint = first & 0x07;
                    continuation_count = 3;
                }
                else
                {
                    return std::nullopt;
                }

                for(size_t idx = 0; idx < continuation_count; ++idx)
                {
                    unsigned char continuation = *src++;
                    if(!is_continuation_byte(continuation))
                    {
                        return std::nullopt;
                    }
                    codepoint = (codepoint << 6) | (continuation & 0x3f);
                }

                if((continuation_count == 1 && codepoint < 0x80) ||
                   (continuation_count == 2 && codepoint < 0x800) ||
                   (continuation_count == 3 && codepoint < 0x10000) ||
                   codepoint > 0x10ffff ||
                   (codepoint >= 0xd800 && codepoint <= 0xdfff))
                {
                    return std::nullopt;
                }

                append_codepoint(result, codepoint);
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

    std::optional<std::wstring> decoded_name = cl::decode_utf8_api_string(name);
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
