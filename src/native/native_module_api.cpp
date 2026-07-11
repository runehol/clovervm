#include "native/native_module_api_internal.h"

#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "builtin_types/unicode.h"
#include "object_model/native_function.h"
#include "object_model/typed_value.h"
#include "runtime/runtime_helpers.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
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
                    L"ImportError", string_value(builder->thread, message));
            }
            return CLOVER_STATUS_ERROR;
        }

        std::optional<TValue<String>>
        decode_module_value_name(clover_native_module_builder *builder,
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
                string_value(builder->thread, *decoded_docstring));
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
            Expected<TValue<Function>> extension_function =
                make_extension_function(builder->thread->get_machine(),
                                        *decoded_name, function,
                                        *decoded_docstring);
            if(extension_function.has_exception())
            {
                return CLOVER_STATUS_ERROR;
            }
            return add_function(builder, *decoded_name,
                                extension_function.value());
        }

    }  // namespace
}  // namespace cl

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
clover_module_add_value(clover_native_module_builder *builder, const char *name,
                        clover_handle value)
{
    if(!cl::valid_builder(builder))
    {
        return CLOVER_STATUS_ERROR;
    }

    std::optional<cl::TValue<cl::String>> decoded_name =
        cl::decode_module_value_name(builder, name, L"value");
    if(!decoded_name.has_value())
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped = cl::unwrap_clover_handle(value);
    if(unwrapped.is_exception_marker())
    {
        if(builder->thread->pending_exception_kind() ==
           cl::PendingExceptionKind::None)
        {
            return cl::set_builder_import_error(
                builder, L"native module value cannot be an exception marker");
        }
        return CLOVER_STATUS_ERROR;
    }

    bool stored = builder->module->set_own_property(*decoded_name, unwrapped);
    if(!stored)
    {
        return cl::set_builder_import_error(
            builder, L"native module could not set value");
    }
    return CLOVER_STATUS_OK;
}
