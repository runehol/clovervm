#include "native/native_module_api_internal.h"

#include "builtin_types/dict.h"
#include "builtin_types/float.h"
#include "builtin_types/list.h"
#include "builtin_types/str.h"
#include "builtin_types/tuple.h"
#include "builtin_types/unicode.h"
#include "object_model/class_object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <optional>
#include <string_view>

namespace cl
{
    namespace
    {
        clover_status set_extension_api_error(clover_context *ctx,
                                              const wchar_t *type_name,
                                              const wchar_t *message)
        {
            if(ctx != nullptr && ctx->thread != nullptr)
            {
                (void)ctx->thread->set_pending_builtin_exception_string(
                    type_name, message);
            }
            return CLOVER_STATUS_ERROR;
        }

        bool valid_context(clover_context *ctx)
        {
            return ctx != nullptr && ctx->thread != nullptr;
        }

        bool unwrap_extension_value(clover_context *ctx, clover_handle handle,
                                    Value &out)
        {
            out = unwrap_clover_handle(handle);
            if(!out.is_vm_sentinel())
            {
                return true;
            }
            if(valid_context(ctx) && !ctx->thread->has_pending_exception())
            {
                (void)set_extension_api_error(
                    ctx, L"TypeError",
                    L"native extension value handle is invalid");
            }
            return false;
        }

        Dict *unwrap_dict(clover_context *ctx, clover_handle handle)
        {
            Value value;
            if(!unwrap_extension_value(ctx, handle, value))
            {
                return nullptr;
            }
            if(!can_convert_to<Dict>(value))
            {
                (void)set_extension_api_error(
                    ctx, L"TypeError", L"value cannot be converted to dict");
                return nullptr;
            }
            return value.get_ptr<Dict>();
        }

        std::optional<Owned<TValue<String>>>
        decode_dict_string_key(clover_context *ctx, const char *key)
        {
            if(!valid_context(ctx) || key == nullptr)
            {
                if(valid_context(ctx))
                {
                    (void)set_extension_api_error(
                        ctx, L"ValueError",
                        L"native extension dict key must not be null");
                }
                return std::nullopt;
            }
            std::optional<TValue<String>> string =
                try_make_string_from_utf8(ctx->thread, std::string_view(key));
            if(!string.has_value())
            {
                (void)set_extension_api_error(
                    ctx, L"ValueError",
                    L"native extension dict key must be valid UTF-8");
                return std::nullopt;
            }
            return Owned<TValue<String>>(*string);
        }

        void initialize_item_outputs(bool *found, clover_handle *out_value)
        {
            if(found != nullptr)
            {
                *found = false;
            }
            if(out_value != nullptr)
            {
                *out_value = wrap_clover_handle(Value::None());
            }
        }

        enum class DictSnapshotKind
        {
            Keys,
            Values,
            Items,
        };

        clover_handle dict_snapshot(clover_context *ctx, clover_handle handle,
                                    DictSnapshotKind kind)
        {
            if(!valid_context(ctx))
            {
                return clover_propagate_error(ctx);
            }
            Dict *dict = unwrap_dict(ctx, handle);
            if(dict == nullptr)
            {
                return clover_propagate_error(ctx);
            }

            Owned<TValue<List>> result(ctx->thread->make_object_value<List>());
            result.extract()->reserve(dict->size());
            for(Dict::EntryView entry: *dict)
            {
                if(kind == DictSnapshotKind::Keys)
                {
                    result.extract()->append(entry.key);
                }
                else if(kind == DictSnapshotKind::Values)
                {
                    result.extract()->append(entry.value);
                }
                else
                {
                    TValue<Tuple> item =
                        ctx->thread->make_object_value<Tuple>(2);
                    item.extract()->initialize_item_unchecked(0, entry.key);
                    item.extract()->initialize_item_unchecked(1, entry.value);
                    result.extract()->append(item.raw_value());
                }
            }
            return wrap_clover_handle(result.raw_value());
        }
    }  // namespace
}  // namespace cl

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
    if(out != nullptr)
    {
        *out = 0;
    }
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped;
    if(!cl::unwrap_extension_value(ctx, value, unwrapped))
    {
        return CLOVER_STATUS_ERROR;
    }
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
    if(out != nullptr)
    {
        *out = cl::wrap_clover_handle(cl::Value::None());
    }
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped;
    if(!cl::unwrap_extension_value(ctx, value, unwrapped))
    {
        return CLOVER_STATUS_ERROR;
    }
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
    if(out_size != nullptr)
    {
        *out_size = 0;
    }
    if(ctx == nullptr || ctx->thread == nullptr || out_size == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped;
    if(!cl::unwrap_extension_value(ctx, value, unwrapped))
    {
        return CLOVER_STATUS_ERROR;
    }
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
    if(out != nullptr)
    {
        *out = 0.0;
    }
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped;
    if(!cl::unwrap_extension_value(ctx, value, unwrapped))
    {
        return CLOVER_STATUS_ERROR;
    }
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
    if(out != nullptr)
    {
        *out = 0;
    }
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped;
    if(!cl::unwrap_extension_value(ctx, value, unwrapped))
    {
        return CLOVER_STATUS_ERROR;
    }
    if(unwrapped.is_smi())
    {
        *out = unwrapped.get_smi();
        return CLOVER_STATUS_OK;
    }

    (void)ctx->thread->set_pending_builtin_exception_string(
        L"TypeError", L"value cannot be converted to int");
    return CLOVER_STATUS_ERROR;
}

extern "C" CL_EXPORT clover_status clover_is(clover_context *ctx,
                                             clover_handle left,
                                             clover_handle right, bool *out)
{
    if(out != nullptr)
    {
        *out = false;
    }
    if(ctx == nullptr || ctx->thread == nullptr || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Value unwrapped_left;
    cl::Value unwrapped_right;
    if(!cl::unwrap_extension_value(ctx, left, unwrapped_left) ||
       !cl::unwrap_extension_value(ctx, right, unwrapped_right))
    {
        return CLOVER_STATUS_ERROR;
    }
    *out = unwrapped_left == unwrapped_right;
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_dict_check(clover_context *ctx,
                                                     clover_handle value,
                                                     bool *out)
{
    if(!cl::valid_context(ctx) || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    *out = false;

    cl::Value unwrapped;
    if(!cl::unwrap_extension_value(ctx, value, unwrapped))
    {
        return CLOVER_STATUS_ERROR;
    }
    *out = cl::can_convert_to<cl::Dict>(unwrapped);
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_dict_check_exact(clover_context *ctx,
                                                           clover_handle value,
                                                           bool *out)
{
    clover_status status = clover_dict_check(ctx, value, out);
    if(status != CLOVER_STATUS_OK)
    {
        return status;
    }
    if(!*out)
    {
        return CLOVER_STATUS_OK;
    }

    cl::Dict *dict = cl::unwrap_clover_handle(value).get_ptr<cl::Dict>();
    *out = dict->get_shape()->get_class() ==
           ctx->thread->get_machine()->dict_class();
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_handle clover_dict_new(clover_context *ctx)
{
    if(!cl::valid_context(ctx))
    {
        return clover_propagate_error(ctx);
    }
    return cl::wrap_clover_handle(
        ctx->thread->make_object_value<cl::Dict>().raw_value());
}

extern "C" CL_EXPORT clover_status clover_dict_clear(clover_context *ctx,
                                                     clover_handle dict)
{
    if(!cl::valid_context(ctx))
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Dict *unwrapped = cl::unwrap_dict(ctx, dict);
    if(unwrapped == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    unwrapped->clear();
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_handle clover_dict_copy(clover_context *ctx,
                                                    clover_handle dict)
{
    if(!cl::valid_context(ctx))
    {
        return clover_propagate_error(ctx);
    }
    cl::Dict *unwrapped = cl::unwrap_dict(ctx, dict);
    if(unwrapped == nullptr)
    {
        return clover_propagate_error(ctx);
    }
    return cl::wrap_clover_handle(unwrapped->copy().raw_value());
}

extern "C" CL_EXPORT clover_status clover_dict_size(clover_context *ctx,
                                                    clover_handle dict,
                                                    size_t *out)
{
    if(!cl::valid_context(ctx) || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Dict *unwrapped = cl::unwrap_dict(ctx, dict);
    if(unwrapped == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    *out = unwrapped->size();
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_dict_contains(clover_context *ctx,
                                                        clover_handle dict,
                                                        clover_handle key,
                                                        bool *out)
{
    if(!cl::valid_context(ctx) || out == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    *out = false;
    cl::Dict *unwrapped_dict = cl::unwrap_dict(ctx, dict);
    cl::Value unwrapped_key;
    if(unwrapped_dict == nullptr ||
       !cl::unwrap_extension_value(ctx, key, unwrapped_key))
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Expected<bool> result =
        unwrapped_dict->contains(ctx->thread, unwrapped_key);
    if(result.has_exception())
    {
        return CLOVER_STATUS_ERROR;
    }
    *out = result.value();
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_dict_set_item(clover_context *ctx,
                                                        clover_handle dict,
                                                        clover_handle key,
                                                        clover_handle value)
{
    if(!cl::valid_context(ctx))
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Dict *unwrapped_dict = cl::unwrap_dict(ctx, dict);
    cl::Value unwrapped_key;
    cl::Value unwrapped_value;
    if(unwrapped_dict == nullptr ||
       !cl::unwrap_extension_value(ctx, key, unwrapped_key) ||
       !cl::unwrap_extension_value(ctx, value, unwrapped_value))
    {
        return CLOVER_STATUS_ERROR;
    }
    return unwrapped_dict->set_item(ctx->thread, unwrapped_key, unwrapped_value)
                   .has_exception()
               ? CLOVER_STATUS_ERROR
               : CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_dict_del_item(clover_context *ctx,
                                                        clover_handle dict,
                                                        clover_handle key)
{
    if(!cl::valid_context(ctx))
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Dict *unwrapped_dict = cl::unwrap_dict(ctx, dict);
    cl::Value unwrapped_key;
    if(unwrapped_dict == nullptr ||
       !cl::unwrap_extension_value(ctx, key, unwrapped_key))
    {
        return CLOVER_STATUS_ERROR;
    }
    return unwrapped_dict->del_item(ctx->thread, unwrapped_key).has_exception()
               ? CLOVER_STATUS_ERROR
               : CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status
clover_dict_get_item(clover_context *ctx, clover_handle dict, clover_handle key,
                     bool *found, clover_handle *out_value)
{
    cl::initialize_item_outputs(found, out_value);
    if(!cl::valid_context(ctx) || found == nullptr || out_value == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Dict *unwrapped_dict = cl::unwrap_dict(ctx, dict);
    cl::Value unwrapped_key;
    if(unwrapped_dict == nullptr ||
       !cl::unwrap_extension_value(ctx, key, unwrapped_key))
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Expected<cl::Dict::ItemResult> result =
        unwrapped_dict->get_item_if_present(ctx->thread, unwrapped_key);
    if(result.has_exception())
    {
        return CLOVER_STATUS_ERROR;
    }
    *found = result.value().found;
    if(*found)
    {
        *out_value = cl::wrap_clover_handle(result.value().value);
    }
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_dict_set_default(
    clover_context *ctx, clover_handle dict, clover_handle key,
    clover_handle default_value, bool *was_present, clover_handle *out_value)
{
    cl::initialize_item_outputs(was_present, out_value);
    if(!cl::valid_context(ctx) || was_present == nullptr ||
       out_value == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Dict *unwrapped_dict = cl::unwrap_dict(ctx, dict);
    cl::Value unwrapped_key;
    cl::Value unwrapped_default;
    if(unwrapped_dict == nullptr ||
       !cl::unwrap_extension_value(ctx, key, unwrapped_key) ||
       !cl::unwrap_extension_value(ctx, default_value, unwrapped_default))
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Expected<cl::Dict::SetDefaultResult> result =
        unwrapped_dict->setdefault_with_presence(ctx->thread, unwrapped_key,
                                                 unwrapped_default);
    if(result.has_exception())
    {
        return CLOVER_STATUS_ERROR;
    }
    *was_present = result.value().was_present;
    *out_value = cl::wrap_clover_handle(result.value().value);
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_dict_pop(clover_context *ctx,
                                                   clover_handle dict,
                                                   clover_handle key,
                                                   bool *found,
                                                   clover_handle *out_value)
{
    cl::initialize_item_outputs(found, out_value);
    if(!cl::valid_context(ctx) || found == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Dict *unwrapped_dict = cl::unwrap_dict(ctx, dict);
    cl::Value unwrapped_key;
    if(unwrapped_dict == nullptr ||
       !cl::unwrap_extension_value(ctx, key, unwrapped_key))
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Expected<cl::Dict::ItemResult> result =
        unwrapped_dict->pop_item_if_present(ctx->thread, unwrapped_key);
    if(result.has_exception())
    {
        return CLOVER_STATUS_ERROR;
    }
    *found = result.value().found;
    if(*found && out_value != nullptr)
    {
        *out_value = cl::wrap_clover_handle(result.value().value);
    }
    return CLOVER_STATUS_OK;
}

extern "C" CL_EXPORT clover_status clover_dict_contains_string(
    clover_context *ctx, clover_handle dict, const char *key, bool *out)
{
    std::optional<cl::Owned<cl::TValue<cl::String>>> string =
        cl::decode_dict_string_key(ctx, key);
    if(!string.has_value())
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_dict_contains(
        ctx, dict, cl::wrap_clover_handle(string->raw_value()), out);
}

extern "C" CL_EXPORT clover_status
clover_dict_set_item_string(clover_context *ctx, clover_handle dict,
                            const char *key, clover_handle value)
{
    std::optional<cl::Owned<cl::TValue<cl::String>>> string =
        cl::decode_dict_string_key(ctx, key);
    if(!string.has_value())
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_dict_set_item(
        ctx, dict, cl::wrap_clover_handle(string->raw_value()), value);
}

extern "C" CL_EXPORT clover_status clover_dict_del_item_string(
    clover_context *ctx, clover_handle dict, const char *key)
{
    std::optional<cl::Owned<cl::TValue<cl::String>>> string =
        cl::decode_dict_string_key(ctx, key);
    if(!string.has_value())
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_dict_del_item(ctx, dict,
                                cl::wrap_clover_handle(string->raw_value()));
}

extern "C" CL_EXPORT clover_status clover_dict_get_item_string(
    clover_context *ctx, clover_handle dict, const char *key, bool *found,
    clover_handle *out_value)
{
    cl::initialize_item_outputs(found, out_value);
    std::optional<cl::Owned<cl::TValue<cl::String>>> string =
        cl::decode_dict_string_key(ctx, key);
    if(!string.has_value())
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_dict_get_item(ctx, dict,
                                cl::wrap_clover_handle(string->raw_value()),
                                found, out_value);
}

extern "C" CL_EXPORT clover_status
clover_dict_pop_string(clover_context *ctx, clover_handle dict, const char *key,
                       bool *found, clover_handle *out_value)
{
    cl::initialize_item_outputs(found, out_value);
    std::optional<cl::Owned<cl::TValue<cl::String>>> string =
        cl::decode_dict_string_key(ctx, key);
    if(!string.has_value())
    {
        return CLOVER_STATUS_ERROR;
    }
    return clover_dict_pop(ctx, dict,
                           cl::wrap_clover_handle(string->raw_value()), found,
                           out_value);
}

extern "C" CL_EXPORT clover_handle clover_dict_keys(clover_context *ctx,
                                                    clover_handle dict)
{
    return cl::dict_snapshot(ctx, dict, cl::DictSnapshotKind::Keys);
}

extern "C" CL_EXPORT clover_handle clover_dict_values(clover_context *ctx,
                                                      clover_handle dict)
{
    return cl::dict_snapshot(ctx, dict, cl::DictSnapshotKind::Values);
}

extern "C" CL_EXPORT clover_handle clover_dict_items(clover_context *ctx,
                                                     clover_handle dict)
{
    return cl::dict_snapshot(ctx, dict, cl::DictSnapshotKind::Items);
}

extern "C" CL_EXPORT clover_status
clover_dict_next(clover_context *ctx, clover_handle dict, size_t *position,
                 bool *found, clover_handle *out_key, clover_handle *out_value)
{
    cl::initialize_item_outputs(found, out_key);
    if(out_value != nullptr)
    {
        *out_value = cl::wrap_clover_handle(cl::Value::None());
    }
    if(!cl::valid_context(ctx) || position == nullptr || found == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }
    cl::Dict *unwrapped = cl::unwrap_dict(ctx, dict);
    if(unwrapped == nullptr)
    {
        return CLOVER_STATUS_ERROR;
    }

    cl::Dict::EntryView entry{cl::Value::None(), cl::Value::None()};
    while(*position < unwrapped->entry_storage_size())
    {
        size_t idx = (*position)++;
        if(!unwrapped->entry_at(idx, entry))
        {
            continue;
        }
        *found = true;
        if(out_key != nullptr)
        {
            *out_key = cl::wrap_clover_handle(entry.key);
        }
        if(out_value != nullptr)
        {
            *out_value = cl::wrap_clover_handle(entry.value);
        }
        return CLOVER_STATUS_OK;
    }
    return CLOVER_STATUS_OK;
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
