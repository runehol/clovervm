#include "native/native_module_api_internal.h"

#include "builtin_types/float.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "builtin_types/unicode.h"
#include "object_model/class_object.h"
#include "object_model/typed_value.h"
#include "runtime/thread_state.h"
#include <optional>
#include <string_view>

extern "C" CL_EXPORT clover_handle clover_none(clover_context *ctx)
{
    (void)ctx;
    return cl::wrap_clover_handle(cl::Value::None());
}

extern "C" CL_EXPORT clover_handle clover_int_from_int64(clover_context *ctx,
                                                         int64_t value)
{
    if(value < cl::value_smi_min || value > cl::value_smi_max)
    {
        if(ctx != nullptr && ctx->thread != nullptr)
        {
            (void)ctx->thread->set_pending_builtin_exception_string(
                L"OverflowError",
                L"integer is outside the supported native API range");
        }
        return clover_propagate_error(ctx);
    }
    return cl::wrap_clover_handle(cl::Value::from_smi(value));
}

extern "C" CL_EXPORT clover_handle clover_float_from_double(clover_context *ctx,
                                                            double value)
{
    if(ctx == nullptr || ctx->thread == nullptr)
    {
        return clover_propagate_error(ctx);
    }
    return cl::wrap_clover_handle(
        ctx->thread->make_object_value<cl::Float>(value).raw_value());
}

extern "C" CL_EXPORT clover_handle
clover_string_from_utf8(clover_context *ctx, const char *utf8_value)
{
    if(ctx == nullptr || ctx->thread == nullptr || utf8_value == nullptr)
    {
        return clover_propagate_error(ctx);
    }

    std::optional<cl::TValue<cl::String>> string =
        cl::try_make_string_from_utf8(ctx->thread,
                                      std::string_view(utf8_value));
    if(!string.has_value())
    {
        (void)ctx->thread->set_pending_builtin_exception_string(
            L"ValueError", L"native extension string must be valid UTF-8");
        return clover_propagate_error(ctx);
    }
    return cl::wrap_clover_handle(string->raw_value());
}

extern "C" CL_EXPORT clover_handle clover_tuple_from_array(
    clover_context *ctx, const clover_handle *items, size_t count)
{
    if(ctx == nullptr || ctx->thread == nullptr)
    {
        return clover_propagate_error(ctx);
    }
    if(items == nullptr && count != 0)
    {
        (void)ctx->thread->set_pending_builtin_exception_string(
            L"ValueError", L"native extension tuple items must not be null");
        return clover_propagate_error(ctx);
    }
    if(count > static_cast<size_t>(cl::value_smi_max))
    {
        (void)ctx->thread->set_pending_builtin_exception_string(
            L"OverflowError",
            L"tuple size is outside the supported native API range");
        return clover_propagate_error(ctx);
    }

    cl::TValue<cl::Tuple> tuple =
        ctx->thread->make_object_value<cl::Tuple>(count);
    for(size_t idx = 0; idx < count; ++idx)
    {
        cl::Value item = cl::unwrap_clover_handle(items[idx]);
        if(item.is_exception_marker())
        {
            return clover_propagate_error(ctx);
        }
        tuple.extract()->initialize_item_unchecked(idx, item);
    }
    return cl::wrap_clover_handle(tuple.raw_value());
}

extern "C" CL_EXPORT clover_handle clover_tuple_from_pair(clover_context *ctx,
                                                          clover_handle item0,
                                                          clover_handle item1)
{
    clover_handle items[] = {item0, item1};
    return clover_tuple_from_array(ctx, items, 2);
}

extern "C" CL_EXPORT clover_status clover_tuple_size(clover_context *ctx,
                                                     clover_handle value,
                                                     size_t *out)
{
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped = cl::unwrap_clover_handle(value);
    if(!cl::can_convert_to<cl::Tuple>(unwrapped))
    {
        (void)ctx->thread->set_pending_builtin_exception_string(
            L"TypeError", L"value cannot be converted to tuple");
        return CLOVER_STATUS_ERROR;
    }

    *out = unwrapped.get_ptr<cl::Tuple>()->size();
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_tuple_get_item(clover_context *ctx,
                                                         clover_handle value,
                                                         size_t index,
                                                         clover_handle *out)
{
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped = cl::unwrap_clover_handle(value);
    if(!cl::can_convert_to<cl::Tuple>(unwrapped))
    {
        (void)ctx->thread->set_pending_builtin_exception_string(
            L"TypeError", L"value cannot be converted to tuple");
        return CLOVER_STATUS_ERROR;
    }

    cl::Tuple *tuple = unwrapped.get_ptr<cl::Tuple>();
    if(index >= tuple->size())
    {
        (void)ctx->thread->set_pending_builtin_exception_string(
            L"IndexError", L"tuple index out of range");
        return CLOVER_STATUS_ERROR;
    }

    *out = cl::wrap_clover_handle(tuple->item_unchecked(index));
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_string_as_utf8(clover_context *ctx,
                                                         clover_handle value,
                                                         char *out,
                                                         size_t out_capacity,
                                                         size_t *out_size)
{
    if(ctx == nullptr || ctx->thread == nullptr || out_size == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped = cl::unwrap_clover_handle(value);
    if(!cl::can_convert_to<cl::String>(unwrapped))
    {
        (void)ctx->thread->set_pending_builtin_exception_string(
            L"TypeError", L"value cannot be converted to str");
        return CLOVER_STATUS_ERROR;
    }

    std::string encoded = cl::unicode::encode_utf8(
        cl::string_view(cl::TValue<cl::String>::from_value_assumed(unwrapped)));
    *out_size = encoded.size();
    if(out == nullptr)
    {
        return CLOVER_STATUS_OK;
    }
    if(out_capacity <= encoded.size())
    {
        (void)ctx->thread->set_pending_builtin_exception_string(
            L"OverflowError", L"native extension string buffer is too small");
        return CLOVER_STATUS_ERROR;
    }
    std::memcpy(out, encoded.data(), encoded.size());
    out[encoded.size()] = '\0';
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_float_as_double(clover_context *ctx,
                                                          clover_handle value,
                                                          double *out)
{
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped = cl::unwrap_clover_handle(value);
    if(unwrapped.is_smi())
    {
        *out = static_cast<double>(unwrapped.get_smi());
        return CLOVER_STATUS_OK;
    }

    if(cl::can_convert_to<cl::Float>(unwrapped))
    {
        *out = unwrapped.get_ptr<cl::Float>()->value;
        return CLOVER_STATUS_OK;
    }

    (void)ctx->thread->set_pending_builtin_exception_string(
        L"TypeError", L"value cannot be converted to float");
    return CLOVER_STATUS_ERROR;
}

extern "C" CL_EXPORT clover_status clover_int_as_int64(clover_context *ctx,
                                                       clover_handle value,
                                                       int64_t *out)
{
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped = cl::unwrap_clover_handle(value);
    if(unwrapped.is_smi())
    {
        *out = unwrapped.get_smi();
        return CLOVER_STATUS_OK;
    }

    (void)ctx->thread->set_pending_builtin_exception_string(
        L"TypeError", L"value cannot be converted to int");
    return CLOVER_STATUS_ERROR;
}

static clover_handle clover_raise_builtin_error(clover_context *ctx,
                                                const wchar_t *type_name,
                                                const char *utf8_message)
{
    if(ctx != nullptr && ctx->thread != nullptr)
    {
        std::optional<std::wstring> decoded_message =
            cl::unicode::decode_utf8_c_string(utf8_message);
        if(decoded_message.has_value())
        {
            (void)ctx->thread->set_pending_builtin_exception_string(
                type_name, decoded_message->c_str());
        }
        else
        {
            (void)ctx->thread->set_pending_builtin_exception_string(
                type_name,
                L"native extension error message must be valid UTF-8");
        }
    }
    return clover_propagate_error(ctx);
}

extern "C" CL_EXPORT clover_handle
clover_raise_overflow_error(clover_context *ctx, const char *utf8_message)
{
    return clover_raise_builtin_error(ctx, L"OverflowError", utf8_message);
}

extern "C" CL_EXPORT clover_handle
clover_raise_value_error(clover_context *ctx, const char *utf8_message)
{
    return clover_raise_builtin_error(ctx, L"ValueError", utf8_message);
}

extern "C" CL_EXPORT clover_handle clover_propagate_error(clover_context *ctx)
{
    (void)ctx;
    return cl::wrap_clover_handle(cl::Value::exception_marker());
}
