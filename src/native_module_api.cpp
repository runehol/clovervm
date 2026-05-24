#include "native_module_api_internal.h"

#include "module_object.h"
#include "native_function.h"
#include "str.h"
#include "thread_state.h"
#include "typed_value.h"
#include "unicode.h"
#include "virtual_machine.h"
#include <string_view>

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

        std::optional<TValue<String>>
        decode_constant_name(clover_native_module_builder *builder,
                             const char *name, const wchar_t *kind)
        {
            std::optional<std::wstring> decoded_name =
                unicode::decode_utf8_c_string(name);
            if(!decoded_name.has_value() || decoded_name->empty())
            {
                std::wstring message = L"native module ";
                message += kind;
                message += L" constant name must be non-empty";
                set_builder_import_error(builder, message);
                return std::nullopt;
            }
            return interned_string(builder->thread, *decoded_name);
        }

        std::optional<TValue<String>>
        decode_function_name(clover_native_module_builder *builder,
                             const char *name)
        {
            std::optional<std::wstring> decoded_name =
                unicode::decode_utf8_c_string(name);
            if(!decoded_name.has_value() || decoded_name->empty())
            {
                set_builder_import_error(
                    builder, L"native module function name must be non-empty");
                return std::nullopt;
            }
            return interned_string(builder->thread, *decoded_name);
        }

        std::optional<Optional<TValue<String>>>
        decode_function_docstring(clover_native_module_builder *builder,
                                  const char *docstring)
        {
            if(docstring == nullptr)
            {
                return Optional<TValue<String>>::none();
            }
            std::optional<std::wstring> decoded_docstring =
                unicode::decode_utf8_c_string(docstring);
            if(!decoded_docstring.has_value())
            {
                set_builder_import_error(
                    builder,
                    L"native module function docstring must be valid UTF-8");
                return std::nullopt;
            }
            return Optional<TValue<String>>::some(
                interned_string(builder->thread, *decoded_docstring));
        }

        bool valid_builder(clover_native_module_builder *builder)
        {
            return builder != nullptr && builder->thread != nullptr &&
                   builder->module != nullptr;
        }

        clover_status add_function(clover_native_module_builder *builder,
                                   TValue<String> name,
                                   cl::TValue<cl::Function> function)
        {
            bool stored =
                builder->module->set_own_property(name, function.raw_value());
            if(!stored)
            {
                return cl::set_builder_import_error(
                    builder, L"native module could not set function");
            }
            return CLOVER_STATUS_OK;
        }

        template <typename FunctionPointer>
        clover_status
        add_extension_function(clover_native_module_builder *builder,
                               const char *name, FunctionPointer function,
                               const char *docstring)
        {
            if(!valid_builder(builder))
            {
                return CLOVER_STATUS_ERROR;
            }
            if(function == nullptr)
            {
                return set_builder_import_error(
                    builder, L"native module function target must be non-null");
            }
            std::optional<TValue<String>> decoded_name =
                decode_function_name(builder, name);
            if(!decoded_name.has_value())
            {
                return CLOVER_STATUS_ERROR;
            }
            std::optional<Optional<TValue<String>>> decoded_docstring =
                decode_function_docstring(builder, docstring);
            if(!decoded_docstring.has_value())
            {
                return CLOVER_STATUS_ERROR;
            }
            return add_function(
                builder, *decoded_name,
                make_extension_function(builder->thread->get_machine(),
                                        *decoded_name, function,
                                        *decoded_docstring));
        }

    }  // namespace
}  // namespace cl

extern "C" CL_EXPORT clover_status clover_module_add_int_constant(
    clover_native_module_builder *builder, const char *name, int64_t value)
{
    if(!cl::valid_builder(builder))
    {
        return CLOVER_STATUS_ERROR;
    }

    std::optional<cl::TValue<cl::String>> decoded_name =
        cl::decode_constant_name(builder, name, L"int");
    if(!decoded_name.has_value())
    {
        return CLOVER_STATUS_ERROR;
    }

    if(value < cl::value_smi_min || value > cl::value_smi_max)
    {
        return cl::set_builder_import_error(
            builder,
            L"native module int constant is outside the supported range");
    }

    bool stored = builder->module->set_own_property(*decoded_name,
                                                    cl::Value::from_smi(value));
    if(!stored)
    {
        return cl::set_builder_import_error(
            builder, L"native module could not set int constant");
    }
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_module_add_function_0(
    clover_native_module_builder *builder, const char *name,
    clover_extension_fn_0 function, const char *docstring)
{
    return cl::add_extension_function(builder, name, function, docstring);
}

extern "C" CL_EXPORT clover_status clover_module_add_function_1(
    clover_native_module_builder *builder, const char *name,
    clover_extension_fn_1 function, const char *docstring)
{
    return cl::add_extension_function(builder, name, function, docstring);
}

extern "C" CL_EXPORT clover_status clover_module_add_function_2(
    clover_native_module_builder *builder, const char *name,
    clover_extension_fn_2 function, const char *docstring)
{
    return cl::add_extension_function(builder, name, function, docstring);
}

extern "C" CL_EXPORT clover_status clover_module_add_function_3(
    clover_native_module_builder *builder, const char *name,
    clover_extension_fn_3 function, const char *docstring)
{
    return cl::add_extension_function(builder, name, function, docstring);
}

extern "C" CL_EXPORT clover_status clover_module_add_function_4(
    clover_native_module_builder *builder, const char *name,
    clover_extension_fn_4 function, const char *docstring)
{
    return cl::add_extension_function(builder, name, function, docstring);
}

extern "C" CL_EXPORT clover_status clover_module_add_function_5(
    clover_native_module_builder *builder, const char *name,
    clover_extension_fn_5 function, const char *docstring)
{
    return cl::add_extension_function(builder, name, function, docstring);
}

extern "C" CL_EXPORT clover_status clover_module_add_function_6(
    clover_native_module_builder *builder, const char *name,
    clover_extension_fn_6 function, const char *docstring)
{
    return cl::add_extension_function(builder, name, function, docstring);
}

extern "C" CL_EXPORT clover_status clover_module_add_function_7(
    clover_native_module_builder *builder, const char *name,
    clover_extension_fn_7 function, const char *docstring)
{
    return cl::add_extension_function(builder, name, function, docstring);
}

extern "C" CL_EXPORT clover_status
clover_module_add_string_constant(clover_native_module_builder *builder,
                                  const char *name, const char *utf8_value)
{
    if(!cl::valid_builder(builder))
    {
        return CLOVER_STATUS_ERROR;
    }

    std::optional<cl::TValue<cl::String>> decoded_name =
        cl::decode_constant_name(builder, name, L"string");
    if(!decoded_name.has_value())
    {
        return CLOVER_STATUS_ERROR;
    }

    if(utf8_value == nullptr)
    {
        return cl::set_builder_import_error(
            builder,
            L"native module string constant value must be valid UTF-8");
    }

    std::optional<cl::TValue<cl::String>> decoded_value =
        cl::try_make_string_from_utf8(builder->thread,
                                      std::string_view(utf8_value));
    if(!decoded_value.has_value())
    {
        return cl::set_builder_import_error(
            builder,
            L"native module string constant value must be valid UTF-8");
    }

    bool stored = builder->module->set_own_property(*decoded_name,
                                                    decoded_value->raw_value());
    if(!stored)
    {
        return cl::set_builder_import_error(
            builder, L"native module could not set string constant");
    }
    return CLOVER_STATUS_OK;
}
