#include "native_module_api_internal.h"

#include "class_object.h"
#include "float.h"
#include "str.h"
#include "thread_state.h"
#include "tuple.h"
#include "typed_value.h"
#include "unicode.h"
#include <optional>
#include <string_view>

extern "C" CL_EXPORT clover_value clover_none(clover_context *ctx)
{
    (void)ctx;
    return cl::wrap_clover_value(cl::Value::None());
}

extern "C" CL_EXPORT clover_value clover_int64(clover_context *ctx,
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
    return cl::wrap_clover_value(cl::Value::from_smi(value));
}

extern "C" CL_EXPORT clover_value clover_float_from_double(clover_context *ctx,
                                                           double value)
{
    if(ctx == nullptr || ctx->thread == nullptr)
    {
        return clover_propagate_error(ctx);
    }
    return cl::wrap_clover_value(
        ctx->thread->make_object_value<cl::Float>(value).raw_value());
}

extern "C" CL_EXPORT clover_value
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
    return cl::wrap_clover_value(string->raw_value());
}

extern "C" CL_EXPORT clover_value clover_tuple_from_array(
    clover_context *ctx, const clover_value *items, size_t count)
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
        cl::Value item = cl::unwrap_clover_value(items[idx]);
        if(item.is_exception_marker())
        {
            return clover_propagate_error(ctx);
        }
        tuple.extract()->initialize_item_unchecked(idx, item);
    }
    return cl::wrap_clover_value(tuple.raw_value());
}

extern "C" CL_EXPORT clover_value clover_tuple2(clover_context *ctx,
                                                clover_value item0,
                                                clover_value item1)
{
    clover_value items[] = {item0, item1};
    return clover_tuple_from_array(ctx, items, 2);
}

extern "C" CL_EXPORT clover_status clover_float_as_double(clover_context *ctx,
                                                          clover_value value,
                                                          double *out)
{
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped = cl::unwrap_clover_value(value);
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

extern "C" CL_EXPORT clover_value
clover_raise_value_error(clover_context *ctx, const char *utf8_message)
{
    if(ctx != nullptr && ctx->thread != nullptr)
    {
        std::optional<std::wstring> decoded_message =
            cl::unicode::decode_utf8_c_string(utf8_message);
        if(decoded_message.has_value())
        {
            (void)ctx->thread->set_pending_builtin_exception_string(
                L"ValueError", decoded_message->c_str());
        }
        else
        {
            (void)ctx->thread->set_pending_builtin_exception_string(
                L"ValueError",
                L"native extension error message must be valid UTF-8");
        }
    }
    return clover_propagate_error(ctx);
}

extern "C" CL_EXPORT clover_value clover_propagate_error(clover_context *ctx)
{
    (void)ctx;
    return cl::wrap_clover_value(cl::Value::exception_marker());
}
