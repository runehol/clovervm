#include "runtime/unicode_error.h"

#include "builtin_types/bytes.h"
#include "builtin_types/int.h"
#include "builtin_types/module_object.h"
#include "builtin_types/str.h"
#include "builtin_types/string_builder.h"
#include "object_model/function.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/runtime_helpers.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>

namespace cl
{
    namespace
    {
        enum class UnicodeErrorKind
        {
            Encode,
            Decode
        };

        [[nodiscard]] Expected<int64_t> position(Value value)
        {
            if(!is_intlike_value(value))
                return Expected<int64_t>::raise_exception(
                    L"TypeError", L"UnicodeError position must be an integer");
            SmiViewStorage storage;
            int64_t result = CL_TRY(
                bigint_to_int64(intlike_value_bigint_view(value, &storage)));
            static_assert(sizeof(ptrdiff_t) == sizeof(int64_t));
            return Expected<int64_t>::ok(result);
        }

        template <UnicodeErrorKind Kind>
        [[nodiscard]] Value native_format(ThreadState *thread, Value encoding,
                                          Value object, Value start_value,
                                          Value end_value, Value reason)
        {
            Owned<Value> encoding_owner(encoding), object_owner(object),
                reason_owner(reason);
            int64_t start = CL_TRY(position(start_value));
            int64_t end = CL_TRY(position(end_value));
            // Construction stores arbitrary objects unchanged. Check the
            // payload before reading character or byte storage.
            if constexpr(Kind == UnicodeErrorKind::Encode)
            {
                if(!has_native_layout<String>(object))
                    return thread->set_pending_builtin_exception_string(
                        L"TypeError",
                        L"UnicodeError 'object' attribute must be a string");
            }
            else if(!has_native_layout<Bytes>(object))
                return thread->set_pending_builtin_exception_string(
                    L"TypeError",
                    L"UnicodeError 'object' attribute must be a bytes");
            size_t length;
            uint32_t character = 0;
            if constexpr(Kind == UnicodeErrorKind::Encode)
                length = native_cast<String>(object)->count.extract();
            else
                length = native_cast<Bytes>(object)->count.extract();
            bool single = start >= 0 && uint64_t(start) < length &&
                          end > start && end - start == 1;
            if(single)
            {
                if constexpr(Kind == UnicodeErrorKind::Encode)
                    character = native_cast<String>(object)->data[start];
                else
                    character = native_cast<Bytes>(object)->data[start];
            }
            StringBuilder builder;
            builder.append_char(L'\'');
            CL_PROPAGATE_EXCEPTION(builder.append_str(encoding_owner.value()));
            builder.append_c_str(Kind == UnicodeErrorKind::Encode
                                     ? L"' codec can't encode character"
                                     : L"' codec can't decode byte");
            if(single)
            {
                if constexpr(Kind == UnicodeErrorKind::Encode)
                {
                    builder.append_c_str(character <= 0xff     ? L" '\\x"
                                         : character <= 0xffff ? L" '\\u"
                                                               : L" '\\U");
                    builder.append_hex(character, character <= 0xff     ? 2
                                                  : character <= 0xffff ? 4
                                                                        : 8);
                    builder.append_char(L'\'');
                }
                else
                {
                    builder.append_c_str(L" 0x");
                    builder.append_hex(character, 2);
                }
                builder.append_c_str(L" in position ");
                builder.append_c_str(std::to_wstring(start).c_str());
            }
            else
            {
                builder.append_c_str(L"s in position ");
                builder.append_c_str(std::to_wstring(start).c_str());
                builder.append_char(L'-');
                // Python retains arbitrary bounds. Avoid signed overflow when
                // displaying the inclusive end; match CPython's wrap at
                // INT64_MIN.
                std::wstring inclusive_end =
                    end == std::numeric_limits<int64_t>::min()
                        ? std::to_wstring(std::numeric_limits<int64_t>::max())
                        : std::to_wstring(end - 1);
                builder.append_c_str(inclusive_end.c_str());
            }
            builder.append_c_str(L": ");
            CL_PROPAGATE_EXCEPTION(builder.append_str(reason_owner.value()));
            return builder.finish();
        }
    }  // namespace

    void install_unicode_error_helpers(VirtualMachine *vm, ModuleObject *module)
    {
        const BuiltinIntrinsicMethod helpers[] = {
            builtin_intrinsic_method(L"_clover_unicode_encode_format",
                                     native_format<UnicodeErrorKind::Encode>),
            builtin_intrinsic_method(L"_clover_unicode_decode_format",
                                     native_format<UnicodeErrorKind::Decode>),
        };
        for(const auto &helper: helpers)
        {
            auto function = unwrap_bootstrap_expected(
                vm, make_intrinsic_function(vm, helper),
                "creating UnicodeError helper");
            unwrap_bootstrap_expected(
                vm,
                module->set_own_property(interned_string(helper.name),
                                         function.raw_value()),
                "installing UnicodeError helper");
        }
    }

    void install_unicode_error_methods(VirtualMachine *vm, ModuleObject *module)
    {
        struct Binding
        {
            const wchar_t *cls;
            const wchar_t *method;
            const wchar_t *helper;
        };
        const Binding bindings[] = {
            {L"UnicodeEncodeError", L"__init__",
             L"_clover_unicode_encode_init"},
            {L"UnicodeDecodeError", L"__init__",
             L"_clover_unicode_decode_init"},
            {L"UnicodeEncodeError", L"__str__", L"_clover_unicode_encode_str"},
            {L"UnicodeDecodeError", L"__str__", L"_clover_unicode_decode_str"},
        };
        for(const auto &binding: bindings)
        {
            Value function =
                module->get_own_property(interned_string(binding.helper));
            if(!has_native_layout<Function>(function))
                fatal("missing managed UnicodeError method");
            unwrap_bootstrap_expected(
                vm,
                active_thread()
                    ->class_for_builtin_name(binding.cls)
                    ->set_own_property(interned_string(binding.method),
                                       function),
                "installing UnicodeError method");
        }
    }
}  // namespace cl
