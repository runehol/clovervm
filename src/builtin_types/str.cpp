#include "builtin_types/str.h"
#include "builtin_types/hash.h"
#include "builtin_types/int.h"
#include "builtin_types/list.h"
#include "builtin_types/slice.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "builtin_types/unicode.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/owned.h"
#include "object_model/value_string.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <algorithm>
#include <cassert>
#include <cwctype>
#include <iterator>
#include <vector>

namespace cl
{
    static Value smi_to_str_string(ThreadState *thread, Value value)
    {
        assert(value.is_smi());

        int64_t smi = value.get_smi();
        uint64_t magnitude;
        bool negative = smi < 0;
        if(negative)
        {
            magnitude = static_cast<uint64_t>(-(smi + 1)) + 1;
        }
        else
        {
            magnitude = static_cast<uint64_t>(smi);
        }

        cl_wchar buffer[32];
        size_t pos = std::size(buffer);
        do
        {
            buffer[--pos] = static_cast<cl_wchar>(L'0' + (magnitude % 10));
            magnitude /= 10;
        }
        while(magnitude != 0);
        if(negative)
        {
            buffer[--pos] = L'-';
        }

        size_t len = std::size(buffer) - pos;
        TValue<String> result = thread->make_object_value<String>(
            TValue<SMI>::from_smi(static_cast<int64_t>(len)));
        for(size_t idx = 0; idx < len; ++idx)
        {
            result.extract()->data[idx] = buffer[pos + idx];
        }
        return result.raw_value();
    }

    void String::install_bootstrap_class(ClassObject *new_cls)
    {
        assert(new_cls != nullptr);
        if(!Object::is_class_bootstrapped())
        {
            Object::install_bootstrap_class(new_cls);
        }
        else
        {
            assert(Object::get_shape()->get_class() == new_cls);
        }
        if(Object::get_shape() == nullptr)
        {
            Object::set_shape(new_cls->get_instance_root_shape());
        }
    }

    static Value native_str_new(ThreadState *thread, Value cls_value, Value obj)
    {
        if(cls_value != Value::from_oop(active_vm()->str_class()))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__new__ expects str as cls");
        }
        if(can_convert_to<String>(obj))
        {
            return obj;
        }
        if(obj.is_smi())
        {
            return smi_to_str_string(thread, obj);
        }
        return value_to_str_string(obj);
    }

    static Value native_str_str(ThreadState *thread, Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__str__ expects a str receiver");
        }
        return self;
    }

    static Value native_str_repr(ThreadState *thread, Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__repr__ expects a str receiver");
        }

        String *str = self.get_ptr<String>();
        StringBuilder builder;
        builder.append_char(L'\'');
        size_t n_chars = size_t(str->count.extract());
        for(size_t idx = 0; idx < n_chars; ++idx)
        {
            switch(str->data[idx])
            {
                case L'\\':
                    builder.append_c_str(L"\\\\");
                    break;
                case L'\'':
                    builder.append_c_str(L"\\'");
                    break;
                case L'\n':
                    builder.append_c_str(L"\\n");
                    break;
                case L'\r':
                    builder.append_c_str(L"\\r");
                    break;
                case L'\t':
                    builder.append_c_str(L"\\t");
                    break;
                default:
                    builder.append_char(str->data[idx]);
                    break;
            }
        }
        builder.append_char(L'\'');
        return builder.finish();
    }

    static Value native_str_len(ThreadState *thread, Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.__len__ expects a str receiver");
        }
        return self.get_ptr<String>()->count.raw_value();
    }

    static Value native_str_hash(ThreadState *thread, Value self)
    {
        if(!can_convert_to<String>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"str.__hash__ expects a str receiver");
        }
        return string_hash_normalized(TValue<String>::from_value_assumed(self))
            .raw_value();
    }

    static Value native_str_add(ThreadState *thread, Value left_value,
                                Value right_value)
    {
        if(!can_convert_to<String>(left_value))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"str.__add__ expects a str receiver");
        }
        if(!can_convert_to<String>(right_value))
        {
            return Value::NotImplemented();
        }

        return left_value.get_ptr<String>()
            ->concat(right_value.get_ptr<String>())
            .raw_value();
    }

    struct StrEqOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"str.__eq__ expects a str receiver";

        Value operator()(ThreadState *thread, TValue<String> left,
                         TValue<String> right) const
        {
            (void)thread;
            return string_eq(left, right) ? Value::True() : Value::False();
        }
    };

    struct StrNeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"str.__ne__ expects a str receiver";

        Value operator()(ThreadState *thread, TValue<String> left,
                         TValue<String> right) const
        {
            (void)thread;
            return string_eq(left, right) ? Value::False() : Value::True();
        }
    };

    struct StrLtOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"str.__lt__ expects a str receiver";

        Value operator()(ThreadState *thread, TValue<String> left,
                         TValue<String> right) const
        {
            (void)thread;
            return string_compare(left, right) < 0 ? Value::True()
                                                   : Value::False();
        }
    };

    struct StrLeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"str.__le__ expects a str receiver";

        Value operator()(ThreadState *thread, TValue<String> left,
                         TValue<String> right) const
        {
            (void)thread;
            return string_compare(left, right) <= 0 ? Value::True()
                                                    : Value::False();
        }
    };

    struct StrGtOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"str.__gt__ expects a str receiver";

        Value operator()(ThreadState *thread, TValue<String> left,
                         TValue<String> right) const
        {
            (void)thread;
            return string_compare(left, right) > 0 ? Value::True()
                                                   : Value::False();
        }
    };

    struct StrGeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"str.__ge__ expects a str receiver";

        Value operator()(ThreadState *thread, TValue<String> left,
                         TValue<String> right) const
        {
            (void)thread;
            return string_compare(left, right) >= 0 ? Value::True()
                                                    : Value::False();
        }
    };

    struct StrAddOperator
    {
        Value operator()(ThreadState *thread, TValue<String> left,
                         TValue<String> right) const
        {
            (void)thread;
            return left.extract()->concat(right.extract()).raw_value();
        }
    };

    template <typename Operator>
    static Value native_str_compare_operator(ThreadState *thread, Value self,
                                             Value other)
    {
        if(!can_convert_to<String>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }
        if(!can_convert_to<String>(other))
        {
            return Value::NotImplemented();
        }
        return Operator{}(thread, TValue<String>::from_value_assumed(self),
                          TValue<String>::from_value_assumed(other));
    }

    template <typename Operator>
    static Value trusted_str_str_operator(ThreadState *thread, Value left_value,
                                          Value right_value)
    {
        return Operator{}(thread,
                          TValue<String>::from_value_assumed(left_value),
                          TValue<String>::from_value_assumed(right_value));
    }

    template <typename Operator>
    static TrustedResolution
    resolve_trusted_str_str_handler(VirtualMachine *vm, ShapeKey operand0_key,
                                    ShapeKey operand1_key)
    {
        ShapeKey str_key = ShapeKey::from_shape(vm->str_instance_root_shape());
        if(operand0_key == str_key && operand1_key == str_key)
        {
            return TrustedResolution::call_trusted(
                trusted_str_str_operator<Operator>);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename NormalOperator, typename ReflectedOperator>
    static TrustedResolution resolve_trusted_str_str_resolver(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(order == TrustedHandlerOperandOrder::Reflected)
        {
            return resolve_trusted_str_str_handler<ReflectedOperator>(
                vm, operand0_key, operand1_key);
        }
        return resolve_trusted_str_str_handler<NormalOperator>(vm, operand0_key,
                                                               operand1_key);
    }

    static Value trusted_str_hash(ThreadState *thread, Value value)
    {
        (void)thread;
        return string_hash_normalized(TValue<String>::from_value_assumed(value))
            .raw_value();
    }

    static TrustedResolution resolve_trusted_str_hash_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        (void)operand1_key;
        (void)order;

        if(requested_arity != TrustedHandlerArity::Unary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        ShapeKey str_key = ShapeKey::from_shape(vm->str_instance_root_shape());
        if(operand0_key == str_key)
        {
            return TrustedResolution::call_trusted(trusted_str_hash);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static Value require_str_receiver(Value self, const wchar_t *method_name)
    {
        if(!can_convert_to<String>(self))
        {
            std::wstring message = L"str.";
            message += method_name;
            message += L" expects a str receiver";
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        return Value::None();
    }

    static Value require_string_argument(Value value, const wchar_t *message)
    {
        if(!can_convert_to<String>(value))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message);
        }
        return Value::None();
    }

    static Value require_smi_index(Value value, const wchar_t *message,
                                   int64_t &out)
    {
        TValue<SMI> index = TValue<SMI>::from_smi(0);
        Expected<IntToSmiStatus> status =
            try_intlike_value_to_smi(value, &index);
        if(status.has_exception())
        {
            return Value::exception_marker();
        }
        if(status.value() == IntToSmiStatus::NotInt)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message);
        }
        out = index.extract();
        return Value::None();
    }

    static size_t normalize_string_bound(int64_t value, int64_t length)
    {
        if(value < 0)
        {
            value += length;
        }
        if(value < 0)
        {
            return 0;
        }
        if(value > length)
        {
            return static_cast<size_t>(length);
        }
        return static_cast<size_t>(value);
    }

    static Value normalize_string_range(Value self, Value start_value,
                                        Value end_value, size_t &start,
                                        size_t &end)
    {
        int64_t length = self.get_ptr<String>()->count.extract();
        int64_t py_start = 0;
        int64_t py_end = length;
        if(!start_value.is_none())
        {
            CL_PROPAGATE_EXCEPTION(require_smi_index(
                start_value, L"slice indices must be integers", py_start));
        }
        if(!end_value.is_none())
        {
            CL_PROPAGATE_EXCEPTION(require_smi_index(
                end_value, L"slice indices must be integers", py_end));
        }
        start = normalize_string_bound(py_start, length);
        end = normalize_string_bound(py_end, length);
        if(end < start)
        {
            end = start;
        }
        return Value::None();
    }

    static Value native_str_getitem(ThreadState *thread, Value self,
                                    Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"__getitem__"));
        if(can_convert_to<Slice>(index_value))
        {
            TValue<Slice> slice =
                TValue<Slice>::from_value_assumed(index_value);
            if(slice.extract()->step.raw_value().is_none())
            {
                NormalizedNonstridedSlice normalized =
                    CL_TRY(normalize_nonstrided_slice_for_length(
                        thread, slice,
                        self.get_ptr<String>()->count.extract()));
                return self.get_ptr<String>()
                    ->get_slice(thread, normalized)
                    .raw_value();
            }
            NormalizedGeneralSlice normalized =
                CL_TRY(normalize_general_slice_for_length(
                    thread, slice, self.get_ptr<String>()->count.extract()));
            return self.get_ptr<String>()
                ->get_slice(thread, normalized)
                .raw_value();
        }
        int64_t py_idx = 0;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            index_value, L"string indices must be integers or slices", py_idx));
        return self.get_ptr<String>()->char_at(thread, py_idx);
    }

    static Value trusted_str_getitem_smi_handler(ThreadState *thread,
                                                 Value self, Value index_value)
    {
        return self.get_ptr<String>()->char_at(thread, index_value.get_smi());
    }

    static Value
    trusted_str_getitem_nonstrided_slice_handler(ThreadState *thread,
                                                 Value self, Value index_value)
    {
        TValue<Slice> slice = TValue<Slice>::from_value_assumed(index_value);
        NormalizedNonstridedSlice normalized =
            CL_TRY(normalize_nonstrided_slice_for_length(
                thread, slice,
                static_cast<int64_t>(self.get_ptr<String>()->count.extract())));
        return self.get_ptr<String>()
            ->get_slice(thread, normalized)
            .raw_value();
    }

    static Value trusted_str_getitem_general_slice_handler(ThreadState *thread,
                                                           Value self,
                                                           Value index_value)
    {
        TValue<Slice> slice = TValue<Slice>::from_value_assumed(index_value);
        NormalizedGeneralSlice normalized =
            CL_TRY(normalize_general_slice_for_length(
                thread, slice,
                static_cast<int64_t>(self.get_ptr<String>()->count.extract())));
        return self.get_ptr<String>()
            ->get_slice(thread, normalized)
            .raw_value();
    }

    static Value trusted_str_contains_handler(ThreadState *thread, Value self,
                                              Value needle_value)
    {
        if(!can_convert_to<String>(needle_value))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"'in <string>' requires string as left operand");
        }
        return self.get_ptr<String>()->find(needle_value.get_ptr<String>()) >= 0
                   ? Value::True()
                   : Value::False();
    }

    static TrustedResolution resolve_trusted_str_getitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(container_key)->get_class() != vm->str_class())
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            return TrustedResolution::call_trusted(
                trusted_str_getitem_smi_handler);
        }
        if(key_key == ShapeKey::from_shape(vm->slice_step_none_shape()))
        {
            return TrustedResolution::call_trusted(
                trusted_str_getitem_nonstrided_slice_handler);
        }
        if(key_key == ShapeKey::from_shape(vm->slice_general_shape()))
        {
            return TrustedResolution::call_trusted(
                trusted_str_getitem_general_slice_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static TrustedResolution resolve_trusted_str_contains_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        (void)key_key;
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(container_key)->get_class() == vm->str_class())
        {
            return TrustedResolution::call_trusted(
                trusted_str_contains_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static Value native_str_lower(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"lower"));
        return self.get_ptr<String>()->lower().raw_value();
    }

    static Value native_str_capitalize(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"capitalize"));
        return self.get_ptr<String>()->capitalize().raw_value();
    }

    static Value native_str_swapcase(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"swapcase"));
        return self.get_ptr<String>()->swapcase().raw_value();
    }

    static Value native_str_upper(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"upper"));
        return self.get_ptr<String>()->upper().raw_value();
    }

    static Value native_str_startswith(ThreadState *thread, Value self,
                                       Value prefix_value, Value start_value,
                                       Value end_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"startswith"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            prefix_value, L"startswith first arg must be str"));
        size_t start = 0;
        size_t end = 0;
        CL_PROPAGATE_EXCEPTION(
            normalize_string_range(self, start_value, end_value, start, end));
        return self.get_ptr<String>()->startswith(
                   prefix_value.get_ptr<String>(), start, end)
                   ? Value::True()
                   : Value::False();
    }

    static Value native_str_endswith(ThreadState *thread, Value self,
                                     Value suffix_value, Value start_value,
                                     Value end_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"endswith"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            suffix_value, L"endswith first arg must be str"));
        size_t start = 0;
        size_t end = 0;
        CL_PROPAGATE_EXCEPTION(
            normalize_string_range(self, start_value, end_value, start, end));
        return self.get_ptr<String>()->endswith(suffix_value.get_ptr<String>(),
                                                start, end)
                   ? Value::True()
                   : Value::False();
    }

    static Value native_str_find(ThreadState *thread, Value self,
                                 Value needle_value, Value start_value,
                                 Value end_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"find"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        size_t start = 0;
        size_t end = 0;
        CL_PROPAGATE_EXCEPTION(
            normalize_string_range(self, start_value, end_value, start, end));
        return Value::from_smi(self.get_ptr<String>()->find(
            needle_value.get_ptr<String>(), start, end));
    }

    static Value native_str_rfind(ThreadState *thread, Value self,
                                  Value needle_value, Value start_value,
                                  Value end_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"rfind"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        size_t start = 0;
        size_t end = 0;
        CL_PROPAGATE_EXCEPTION(
            normalize_string_range(self, start_value, end_value, start, end));
        return Value::from_smi(self.get_ptr<String>()->rfind(
            needle_value.get_ptr<String>(), start, end));
    }

    static Value native_str_contains(ThreadState *thread, Value self,
                                     Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"__contains__"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            needle_value, L"'in <string>' requires string as left operand"));
        return self.get_ptr<String>()->find(needle_value.get_ptr<String>()) >= 0
                   ? Value::True()
                   : Value::False();
    }

    static Value native_str_index(ThreadState *thread, Value self,
                                  Value needle_value, Value start_value,
                                  Value end_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"index"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        size_t start = 0;
        size_t end = 0;
        CL_PROPAGATE_EXCEPTION(
            normalize_string_range(self, start_value, end_value, start, end));
        return self.get_ptr<String>()->index(needle_value.get_ptr<String>(),
                                             start, end);
    }

    static Value native_str_rindex(ThreadState *thread, Value self,
                                   Value needle_value, Value start_value,
                                   Value end_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"rindex"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        size_t start = 0;
        size_t end = 0;
        CL_PROPAGATE_EXCEPTION(
            normalize_string_range(self, start_value, end_value, start, end));
        return self.get_ptr<String>()->rindex(needle_value.get_ptr<String>(),
                                              start, end);
    }

    static Value native_str_count(ThreadState *thread, Value self,
                                  Value needle_value, Value start_value,
                                  Value end_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"count"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(needle_value, L"must be str, not other"));
        size_t start = 0;
        size_t end = 0;
        CL_PROPAGATE_EXCEPTION(
            normalize_string_range(self, start_value, end_value, start, end));
        return Value::from_smi(self.get_ptr<String>()->count_substring(
            needle_value.get_ptr<String>(), start, end));
    }

    static Value native_str_removeprefix(ThreadState *thread, Value self,
                                         Value prefix_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"removeprefix"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            prefix_value, L"removeprefix first arg must be str"));
        return self.get_ptr<String>()
            ->removeprefix(prefix_value.get_ptr<String>())
            .raw_value();
    }

    static Value native_str_removesuffix(ThreadState *thread, Value self,
                                         Value suffix_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"removesuffix"));
        CL_PROPAGATE_EXCEPTION(require_string_argument(
            suffix_value, L"removesuffix first arg must be str"));
        return self.get_ptr<String>()
            ->removesuffix(suffix_value.get_ptr<String>())
            .raw_value();
    }

    static Value require_maxsplit(Value value, int64_t &out)
    {
        return require_smi_index(value, L"maxsplit must be an integer", out);
    }

    static void append_string_slice(TValue<List> result,
                                    std::wstring_view slice)
    {
        result.extract()->append(
            active_thread()->make_object_value<String>(slice).raw_value());
    }

    static Value split_empty_separator_error()
    {
        return active_thread()->set_pending_builtin_exception_string(
            L"ValueError", L"empty separator");
    }

    static bool is_strip_space(wchar_t ch);

    static Value split_whitespace(Value self, int64_t maxsplit, bool reverse)
    {
        std::wstring_view view(self.get_ptr<String>()->data,
                               size_t(self.get_ptr<String>()->count.extract()));
        Owned<TValue<List>> result(active_thread()->make_object_value<List>());
        std::vector<std::wstring_view> pieces;
        if(!reverse)
        {
            size_t pos = 0;
            int64_t splits = 0;
            while(pos < view.size())
            {
                while(pos < view.size() && is_strip_space(view[pos]))
                {
                    ++pos;
                }
                if(pos == view.size())
                {
                    break;
                }
                if(maxsplit >= 0 && splits == maxsplit)
                {
                    append_string_slice(result.value(), view.substr(pos));
                    return result.value().raw_value();
                }
                size_t start = pos;
                while(pos < view.size() && !is_strip_space(view[pos]))
                {
                    ++pos;
                }
                append_string_slice(result.value(),
                                    view.substr(start, pos - start));
                ++splits;
            }
            return result.value().raw_value();
        }

        size_t end = view.size();
        int64_t splits = 0;
        while(end > 0)
        {
            while(end > 0 && is_strip_space(view[end - 1]))
            {
                --end;
            }
            if(end == 0)
            {
                break;
            }
            if(maxsplit >= 0 && splits == maxsplit)
            {
                size_t remaining_start = 0;
                if(splits > 0)
                {
                    while(remaining_start < end &&
                          is_strip_space(view[remaining_start]))
                    {
                        ++remaining_start;
                    }
                }
                pieces.push_back(
                    view.substr(remaining_start, end - remaining_start));
                break;
            }
            size_t start = end;
            while(start > 0 && !is_strip_space(view[start - 1]))
            {
                --start;
            }
            pieces.push_back(view.substr(start, end - start));
            end = start;
            ++splits;
        }
        for(auto it = pieces.rbegin(); it != pieces.rend(); ++it)
        {
            append_string_slice(result.value(), *it);
        }
        return result.value().raw_value();
    }

    static Value split_explicit(Value self, const String *separator,
                                int64_t maxsplit, bool reverse)
    {
        std::wstring_view view(self.get_ptr<String>()->data,
                               size_t(self.get_ptr<String>()->count.extract()));
        std::wstring_view sep_view(separator->data,
                                   size_t(separator->count.extract()));
        if(sep_view.empty())
        {
            return split_empty_separator_error();
        }

        Owned<TValue<List>> result(active_thread()->make_object_value<List>());
        if(!reverse)
        {
            size_t pos = 0;
            int64_t splits = 0;
            while(maxsplit < 0 || splits < maxsplit)
            {
                size_t found = view.find(sep_view, pos);
                if(found == std::wstring_view::npos)
                {
                    break;
                }
                append_string_slice(result.value(),
                                    view.substr(pos, found - pos));
                pos = found + sep_view.size();
                ++splits;
            }
            append_string_slice(result.value(), view.substr(pos));
            return result.value().raw_value();
        }

        std::vector<std::wstring_view> pieces;
        size_t end = view.size();
        int64_t splits = 0;
        while((maxsplit < 0 || splits < maxsplit) && end >= sep_view.size())
        {
            size_t found = view.rfind(sep_view, end - sep_view.size());
            if(found == std::wstring_view::npos)
            {
                break;
            }
            pieces.push_back(view.substr(found + sep_view.size(),
                                         end - found - sep_view.size()));
            end = found;
            ++splits;
        }
        pieces.push_back(view.substr(0, end));
        for(auto it = pieces.rbegin(); it != pieces.rend(); ++it)
        {
            append_string_slice(result.value(), *it);
        }
        return result.value().raw_value();
    }

    static Value native_str_split(ThreadState *thread, Value self,
                                  Value sep_value, Value maxsplit_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"split"));
        int64_t maxsplit = -1;
        CL_PROPAGATE_EXCEPTION(require_maxsplit(maxsplit_value, maxsplit));
        if(sep_value.is_none())
        {
            return split_whitespace(self, maxsplit, false);
        }
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(sep_value, L"split arg must be str"));
        return split_explicit(self, sep_value.get_ptr<String>(), maxsplit,
                              false);
    }

    static Value native_str_rsplit(ThreadState *thread, Value self,
                                   Value sep_value, Value maxsplit_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"rsplit"));
        int64_t maxsplit = -1;
        CL_PROPAGATE_EXCEPTION(require_maxsplit(maxsplit_value, maxsplit));
        if(sep_value.is_none())
        {
            return split_whitespace(self, maxsplit, true);
        }
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(sep_value, L"rsplit arg must be str"));
        return split_explicit(self, sep_value.get_ptr<String>(), maxsplit,
                              true);
    }

    static Value partition_string(Value self, Value sep_value, bool reverse)
    {
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(sep_value, L"separator must be str"));
        String *str = self.get_ptr<String>();
        String *separator = sep_value.get_ptr<String>();
        std::wstring_view view(str->data, size_t(str->count.extract()));
        std::wstring_view sep_view(separator->data,
                                   size_t(separator->count.extract()));
        if(sep_view.empty())
        {
            return split_empty_separator_error();
        }
        size_t found = reverse ? view.rfind(sep_view) : view.find(sep_view);

        Owned<TValue<Tuple>> result(
            active_thread()->make_object_value<Tuple>(3));
        if(found == std::wstring_view::npos)
        {
            if(reverse)
            {
                result.extract()->initialize_item_unchecked(
                    0, active_thread()
                           ->make_object_value<String>(std::wstring_view())
                           .raw_value());
                result.extract()->initialize_item_unchecked(
                    1, active_thread()
                           ->make_object_value<String>(std::wstring_view())
                           .raw_value());
                result.extract()->initialize_item_unchecked(2, self);
            }
            else
            {
                result.extract()->initialize_item_unchecked(0, self);
                result.extract()->initialize_item_unchecked(
                    1, active_thread()
                           ->make_object_value<String>(std::wstring_view())
                           .raw_value());
                result.extract()->initialize_item_unchecked(
                    2, active_thread()
                           ->make_object_value<String>(std::wstring_view())
                           .raw_value());
            }
            return result.value().raw_value();
        }
        result.extract()->initialize_item_unchecked(
            0, active_thread()
                   ->make_object_value<String>(view.substr(0, found))
                   .raw_value());
        result.extract()->initialize_item_unchecked(1, sep_value);
        result.extract()->initialize_item_unchecked(
            2, active_thread()
                   ->make_object_value<String>(
                       view.substr(found + sep_view.size()))
                   .raw_value());
        return result.value().raw_value();
    }

    static Value native_str_partition(ThreadState *thread, Value self,
                                      Value sep_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"partition"));
        return partition_string(self, sep_value, false);
    }

    static Value native_str_rpartition(ThreadState *thread, Value self,
                                       Value sep_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"rpartition"));
        return partition_string(self, sep_value, true);
    }

    static Value native_str_replace(ThreadState *thread, Value self,
                                    Value old_value, Value new_value,
                                    Value count_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"replace"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(old_value, L"replace old must be str"));
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(new_value, L"replace new must be str"));
        int64_t max_count = -1;
        CL_PROPAGATE_EXCEPTION(require_smi_index(
            count_value, L"replace count must be an integer", max_count));
        return self.get_ptr<String>()
            ->replace(old_value.get_ptr<String>(), new_value.get_ptr<String>(),
                      max_count)
            .raw_value();
    }

    static bool is_strip_space(wchar_t ch) { return std::iswspace(ch) != 0; }

    static Value native_str_strip(ThreadState *thread, Value self,
                                  Value chars_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"strip"));
        if(chars_value.is_none())
        {
            return self.get_ptr<String>()->strip().raw_value();
        }
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(chars_value, L"strip arg must be str"));
        return self.get_ptr<String>()
            ->strip(chars_value.get_ptr<String>())
            .raw_value();
    }

    static Value native_str_lstrip(ThreadState *thread, Value self,
                                   Value chars_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"lstrip"));
        if(chars_value.is_none())
        {
            return self.get_ptr<String>()->lstrip().raw_value();
        }
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(chars_value, L"lstrip arg must be str"));
        return self.get_ptr<String>()
            ->lstrip(chars_value.get_ptr<String>())
            .raw_value();
    }

    static Value native_str_rstrip(ThreadState *thread, Value self,
                                   Value chars_value)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"rstrip"));
        if(chars_value.is_none())
        {
            return self.get_ptr<String>()->rstrip().raw_value();
        }
        CL_PROPAGATE_EXCEPTION(
            require_string_argument(chars_value, L"rstrip arg must be str"));
        return self.get_ptr<String>()
            ->rstrip(chars_value.get_ptr<String>())
            .raw_value();
    }

    static bool is_string_sequence(Value value)
    {
        return can_convert_to<List>(value) || can_convert_to<Tuple>(value);
    }

    static Value native_str_join(ThreadState *thread, Value self,
                                 Value sequence)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"join"));
        if(!is_string_sequence(sequence))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", L"str.join expects a list or tuple");
        }

        if(can_convert_to<List>(sequence))
        {
            List *list = sequence.get_ptr<List>();
            for(size_t idx = 0; idx < list->size(); ++idx)
            {
                CL_PROPAGATE_EXCEPTION(require_string_argument(
                    list->item_unchecked(idx), L"sequence item must be str"));
            }
            return self.get_ptr<String>()->join_list(list).raw_value();
        }

        Tuple *tuple = sequence.get_ptr<Tuple>();
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            CL_PROPAGATE_EXCEPTION(require_string_argument(
                tuple->item_unchecked(idx), L"sequence item must be str"));
        }
        return self.get_ptr<String>()->join_tuple(tuple).raw_value();
    }

    static Value native_str_isalpha(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isalpha"));
        return self.get_ptr<String>()->isalpha() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_isascii(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isascii"));
        return self.get_ptr<String>()->isascii() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_isdigit(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isdigit"));
        return self.get_ptr<String>()->isdigit() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_isalnum(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isalnum"));
        return self.get_ptr<String>()->isalnum() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_islower(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"islower"));
        return self.get_ptr<String>()->islower() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_isprintable(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isprintable"));
        return self.get_ptr<String>()->isprintable() ? Value::True()
                                                     : Value::False();
    }

    static Value native_str_isspace(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isspace"));
        return self.get_ptr<String>()->isspace() ? Value::True()
                                                 : Value::False();
    }

    static Value native_str_isupper(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_str_receiver(self, L"isupper"));
        return self.get_ptr<String>()->isupper() ? Value::True()
                                                 : Value::False();
    }

    Value String::char_at(ThreadState *thread, int64_t py_idx) const
    {
        int64_t length = count.extract();
        int64_t normalized = py_idx;
        if(normalized < 0)
        {
            normalized += length;
        }
        if(normalized < 0 || normalized >= length)
        {
            return thread->set_pending_builtin_exception_string(
                L"IndexError", L"string index out of range");
        }
        std::wstring_view result(&data[static_cast<size_t>(normalized)], 1);
        return thread->make_object_value<String>(result).raw_value();
    }

    TValue<String>
    String::get_slice(ThreadState *thread,
                      const NormalizedNonstridedSlice &slice) const
    {
        return thread->make_object_value<String>(
            std::wstring_view(&data[static_cast<size_t>(slice.start)],
                              slice.selected_sequence_length));
    }

    TValue<String> String::get_slice(ThreadState *thread,
                                     const NormalizedGeneralSlice &slice) const
    {
        TValue<String> result =
            thread->make_object_value<String>(TValue<SMI>::from_smi(
                static_cast<int64_t>(slice.selected_sequence_length)));
        int64_t read_idx = slice.start;
        for(size_t write_idx = 0; write_idx < slice.selected_sequence_length;
            ++write_idx)
        {
            result.extract()->data[write_idx] =
                data[static_cast<size_t>(read_idx)];
            read_idx += slice.step;
        }
        result.extract()->data[slice.selected_sequence_length] = 0;
        return result;
    }

    TValue<String> String::concat(const String *other) const
    {
        std::wstring result(data, size_t(count.extract()));
        result.append(other->data, size_t(other->count.extract()));
        return active_thread()->make_object_value<String>(result);
    }

    TValue<String> String::capitalize() const
    {
        std::wstring result(data, size_t(count.extract()));
        if(!result.empty())
        {
            result[0] = static_cast<wchar_t>(std::towupper(result[0]));
            for(size_t idx = 1; idx < result.size(); ++idx)
            {
                result[idx] = static_cast<wchar_t>(std::towlower(result[idx]));
            }
        }
        return active_thread()->make_object_value<String>(result);
    }

    TValue<String> String::lower() const
    {
        std::wstring result(data, size_t(count.extract()));
        for(wchar_t &ch: result)
        {
            ch = static_cast<wchar_t>(std::towlower(ch));
        }
        return active_thread()->make_object_value<String>(result);
    }

    TValue<String> String::swapcase() const
    {
        std::wstring result(data, size_t(count.extract()));
        for(wchar_t &ch: result)
        {
            if(std::iswlower(ch) != 0)
            {
                ch = static_cast<wchar_t>(std::towupper(ch));
            }
            else if(std::iswupper(ch) != 0)
            {
                ch = static_cast<wchar_t>(std::towlower(ch));
            }
        }
        return active_thread()->make_object_value<String>(result);
    }

    TValue<String> String::upper() const
    {
        std::wstring result(data, size_t(count.extract()));
        for(wchar_t &ch: result)
        {
            ch = static_cast<wchar_t>(std::towupper(ch));
        }
        return active_thread()->make_object_value<String>(result);
    }

    bool String::startswith(const String *prefix) const
    {
        return startswith(prefix, 0, size_t(count.extract()));
    }

    bool String::startswith(const String *prefix, size_t start,
                            size_t end) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view prefix_view(prefix->data,
                                      size_t(prefix->count.extract()));
        std::wstring_view range = str.substr(start, end - start);
        return range.size() >= prefix_view.size() &&
               range.substr(0, prefix_view.size()) == prefix_view;
    }

    bool String::endswith(const String *suffix) const
    {
        return endswith(suffix, 0, size_t(count.extract()));
    }

    bool String::endswith(const String *suffix, size_t start, size_t end) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view suffix_view(suffix->data,
                                      size_t(suffix->count.extract()));
        std::wstring_view range = str.substr(start, end - start);
        return range.size() >= suffix_view.size() &&
               range.substr(range.size() - suffix_view.size()) == suffix_view;
    }

    int64_t String::find(const String *needle) const
    {
        return find(needle, 0, size_t(count.extract()));
    }

    int64_t String::find(const String *needle, size_t start, size_t end) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view needle_view(needle->data,
                                      size_t(needle->count.extract()));
        std::wstring_view range = str.substr(start, end - start);
        size_t found = range.find(needle_view);
        if(found == std::wstring_view::npos)
        {
            return -1;
        }
        return static_cast<int64_t>(start + found);
    }

    int64_t String::rfind(const String *needle) const
    {
        return rfind(needle, 0, size_t(count.extract()));
    }

    int64_t String::rfind(const String *needle, size_t start, size_t end) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view needle_view(needle->data,
                                      size_t(needle->count.extract()));
        std::wstring_view range = str.substr(start, end - start);
        size_t found = range.rfind(needle_view);
        if(found == std::wstring_view::npos)
        {
            return -1;
        }
        return static_cast<int64_t>(start + found);
    }

    Value String::index(const String *needle) const
    {
        return index(needle, 0, size_t(count.extract()));
    }

    Value String::index(const String *needle, size_t start, size_t end) const
    {
        int64_t found = find(needle, start, end);
        if(found == -1)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"substring not found");
        }
        return Value::from_smi(found);
    }

    Value String::rindex(const String *needle) const
    {
        return rindex(needle, 0, size_t(count.extract()));
    }

    Value String::rindex(const String *needle, size_t start, size_t end) const
    {
        int64_t found = rfind(needle, start, end);
        if(found == -1)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"substring not found");
        }
        return Value::from_smi(found);
    }

    int64_t String::count_substring(const String *needle) const
    {
        return count_substring(needle, 0, size_t(count.extract()));
    }

    int64_t String::count_substring(const String *needle, size_t start,
                                    size_t end) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view needle_view(needle->data,
                                      size_t(needle->count.extract()));
        std::wstring_view range = str.substr(start, end - start);
        if(needle_view.empty())
        {
            return static_cast<int64_t>(range.size() + 1);
        }
        int64_t result = 0;
        size_t pos = 0;
        while(pos <= range.size())
        {
            size_t found = range.find(needle_view, pos);
            if(found == std::wstring_view::npos)
            {
                break;
            }
            ++result;
            pos = found + needle_view.size();
        }
        return result;
    }

    TValue<String> String::removeprefix(const String *prefix) const
    {
        if(startswith(prefix))
        {
            size_t prefix_len = size_t(prefix->count.extract());
            std::wstring_view str(data, size_t(count.extract()));
            return active_thread()->make_object_value<String>(
                str.substr(prefix_len));
        }
        return active_thread()->make_object_value<String>(
            std::wstring_view(data, size_t(count.extract())));
    }

    TValue<String> String::removesuffix(const String *suffix) const
    {
        if(endswith(suffix))
        {
            size_t suffix_len = size_t(suffix->count.extract());
            std::wstring_view str(data, size_t(count.extract()));
            return active_thread()->make_object_value<String>(
                str.substr(0, str.size() - suffix_len));
        }
        return active_thread()->make_object_value<String>(
            std::wstring_view(data, size_t(count.extract())));
    }

    TValue<String> String::replace(const String *old, const String *replacement,
                                   int64_t max_count) const
    {
        std::wstring_view str(data, size_t(count.extract()));
        std::wstring_view old_view(old->data, size_t(old->count.extract()));
        std::wstring_view replacement_view(
            replacement->data, size_t(replacement->count.extract()));

        if(max_count == 0)
        {
            return active_thread()->make_object_value<String>(str);
        }

        std::wstring result;
        if(old_view.empty())
        {
            int64_t replacements = 0;
            if(max_count < 0 || replacements < max_count)
            {
                result.append(replacement_view);
                ++replacements;
            }
            for(wchar_t ch: str)
            {
                result.push_back(ch);
                if(max_count < 0 || replacements < max_count)
                {
                    result.append(replacement_view);
                    ++replacements;
                }
            }
            return active_thread()->make_object_value<String>(result);
        }

        size_t pos = 0;
        int64_t replacements = 0;
        while(pos < str.size())
        {
            if(max_count >= 0 && replacements == max_count)
            {
                result.append(str.substr(pos));
                break;
            }
            size_t found = str.find(old_view, pos);
            if(found == std::wstring_view::npos)
            {
                result.append(str.substr(pos));
                break;
            }
            result.append(str.substr(pos, found - pos));
            result.append(replacement_view);
            pos = found + old_view.size();
            ++replacements;
        }
        if(str.empty())
        {
            result.append(str);
        }
        return active_thread()->make_object_value<String>(result);
    }

    static bool string_contains_char(std::wstring_view chars, wchar_t ch)
    {
        return chars.find(ch) != std::wstring_view::npos;
    }

    static TValue<String> strip_string(const String *str, bool strip_left,
                                       bool strip_right,
                                       const String *chars = nullptr)
    {
        std::wstring_view view(str->data, size_t(str->count.extract()));
        std::wstring_view chars_view;
        if(chars != nullptr)
        {
            chars_view =
                std::wstring_view(chars->data, size_t(chars->count.extract()));
        }
        size_t start = 0;
        size_t end = view.size();
        if(strip_left)
        {
            while(start < end &&
                  (chars == nullptr
                       ? is_strip_space(view[start])
                       : string_contains_char(chars_view, view[start])))
            {
                ++start;
            }
        }
        if(strip_right)
        {
            while(end > start &&
                  (chars == nullptr
                       ? is_strip_space(view[end - 1])
                       : string_contains_char(chars_view, view[end - 1])))
            {
                --end;
            }
        }
        return active_thread()->make_object_value<String>(
            std::wstring(view.substr(start, end - start)));
    }

    TValue<String> String::strip() const
    {
        return strip_string(this, true, true);
    }

    TValue<String> String::strip(const String *chars) const
    {
        return strip_string(this, true, true, chars);
    }

    TValue<String> String::lstrip() const
    {
        return strip_string(this, true, false);
    }

    TValue<String> String::lstrip(const String *chars) const
    {
        return strip_string(this, true, false, chars);
    }

    TValue<String> String::rstrip() const
    {
        return strip_string(this, false, true);
    }

    TValue<String> String::rstrip(const String *chars) const
    {
        return strip_string(this, false, true, chars);
    }

    static void append_join_item(std::wstring &result,
                                 std::wstring_view separator, bool need_sep,
                                 Value item)
    {
        if(need_sep)
        {
            result.append(separator);
        }
        result.append(string_view(TValue<String>::from_value_assumed(item)));
    }

    TValue<String> String::join_list(const List *sequence) const
    {
        std::wstring_view separator(data, size_t(count.extract()));
        std::wstring result;
        for(size_t idx = 0; idx < sequence->size(); ++idx)
        {
            append_join_item(result, separator, idx != 0,
                             sequence->item_unchecked(idx));
        }
        return active_thread()->make_object_value<String>(result);
    }

    TValue<String> String::join_tuple(const Tuple *sequence) const
    {
        std::wstring_view separator(data, size_t(count.extract()));
        std::wstring result;
        for(size_t idx = 0; idx < sequence->size(); ++idx)
        {
            append_join_item(result, separator, idx != 0,
                             sequence->item_unchecked(idx));
        }
        return active_thread()->make_object_value<String>(result);
    }

    static bool classify_string(const String *str,
                                int (*predicate)(std::wint_t))
    {
        if(str->count.extract() == 0)
        {
            return false;
        }
        for(size_t idx = 0; idx < size_t(str->count.extract()); ++idx)
        {
            if(predicate(str->data[idx]) == 0)
            {
                return false;
            }
        }
        return true;
    }

    bool String::isascii() const
    {
        for(size_t idx = 0; idx < size_t(count.extract()); ++idx)
        {
            if(data[idx] > 0x7f)
            {
                return false;
            }
        }
        return true;
    }

    bool String::isalpha() const
    {
        return classify_string(this, std::iswalpha);
    }

    bool String::isdigit() const
    {
        return classify_string(this, std::iswdigit);
    }

    bool String::isalnum() const
    {
        return classify_string(this, std::iswalnum);
    }

    static bool classify_cased_string(const String *str,
                                      int (*cased_predicate)(std::wint_t))
    {
        bool saw_cased = false;
        for(size_t idx = 0; idx < size_t(str->count.extract()); ++idx)
        {
            wchar_t ch = str->data[idx];
            if(std::iswlower(ch) != 0 || std::iswupper(ch) != 0)
            {
                saw_cased = true;
                if(cased_predicate(ch) == 0)
                {
                    return false;
                }
            }
        }
        return saw_cased;
    }

    bool String::islower() const
    {
        return classify_cased_string(this, std::iswlower);
    }

    bool String::isprintable() const
    {
        for(size_t idx = 0; idx < size_t(count.extract()); ++idx)
        {
            if(std::iswprint(data[idx]) == 0)
            {
                return false;
            }
        }
        return true;
    }

    bool String::isspace() const
    {
        return classify_string(this, std::iswspace);
    }

    bool String::isupper() const
    {
        return classify_cased_string(this, std::iswupper);
    }

    BuiltinClassDefinition make_str_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::String};
        ClassObject *cls = ClassObject::make_bootstrap_builtin_class<String>(
            vm->get_or_create_interned_string_value(L"str"), 1, nullptr, 0);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_str_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> str_new_defaults(
            active_thread()->make_object_value<Tuple>(1));
        str_new_defaults.extract()->initialize_item_unchecked(
            0, vm->get_or_create_interned_string_value(L"").raw_value());
        Owned<TValue<Tuple>> str_strip_defaults(
            active_thread()->make_object_value<Tuple>(1));
        str_strip_defaults.extract()->initialize_item_unchecked(0,
                                                                Value::None());
        Owned<TValue<Tuple>> str_start_end_defaults(
            active_thread()->make_object_value<Tuple>(2));
        str_start_end_defaults.extract()->initialize_item_unchecked(
            0, Value::None());
        str_start_end_defaults.extract()->initialize_item_unchecked(
            1, Value::None());
        Owned<TValue<Tuple>> str_split_defaults(
            active_thread()->make_object_value<Tuple>(2));
        str_split_defaults.extract()->initialize_item_unchecked(0,
                                                                Value::None());
        str_split_defaults.extract()->initialize_item_unchecked(
            1, Value::from_smi(-1));
        Owned<TValue<Tuple>> str_replace_defaults(
            active_thread()->make_object_value<Tuple>(1));
        str_replace_defaults.extract()->initialize_item_unchecked(
            0, Value::from_smi(-1));
        static constexpr const wchar_t *prefix_start_end_names[] = {
            L"prefix", L"start", L"end"};
        static constexpr const wchar_t *suffix_start_end_names[] = {
            L"suffix", L"start", L"end"};
        static constexpr const wchar_t *sub_start_end_names[] = {
            L"sub", L"start", L"end"};
        static constexpr const wchar_t *chars_names[] = {L"chars"};
        static constexpr const wchar_t *prefix_names[] = {L"prefix"};
        static constexpr const wchar_t *suffix_names[] = {L"suffix"};
        static constexpr const wchar_t *sep_names[] = {L"sep"};
        static constexpr const wchar_t *sep_maxsplit_names[] = {L"sep",
                                                                L"maxsplit"};
        static constexpr const wchar_t *replace_names[] = {L"old", L"new",
                                                           L"count"};
        BuiltinIntrinsicMethod methods[] = {
            with_defaults(builtin_intrinsic_method(L"__new__", native_str_new,
                                                   L"Create a str object."),
                          str_new_defaults.value()),
            builtin_intrinsic_method(L"__str__", native_str_str,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_str_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__len__", native_str_len,
                                     L"Return len(self)."),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__hash__", native_str_hash,
                                         L"Return hash(self)."),
                resolve_trusted_str_hash_handler),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__add__", native_str_add,
                                         L"Return self + value."),
                resolve_trusted_str_str_resolver<StrAddOperator,
                                                 StrAddOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__eq__", native_str_compare_operator<StrEqOperator>,
                    L"Return self == value."),
                resolve_trusted_str_str_resolver<StrEqOperator, StrEqOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__ne__", native_str_compare_operator<StrNeOperator>,
                    L"Return self != value."),
                resolve_trusted_str_str_resolver<StrNeOperator, StrNeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__lt__", native_str_compare_operator<StrLtOperator>,
                    L"Return self < value."),
                resolve_trusted_str_str_resolver<StrLtOperator, StrGtOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__le__", native_str_compare_operator<StrLeOperator>,
                    L"Return self <= value."),
                resolve_trusted_str_str_resolver<StrLeOperator, StrGeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__gt__", native_str_compare_operator<StrGtOperator>,
                    L"Return self > value."),
                resolve_trusted_str_str_resolver<StrGtOperator, StrLtOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__ge__", native_str_compare_operator<StrGeOperator>,
                    L"Return self >= value."),
                resolve_trusted_str_str_resolver<StrGeOperator, StrLeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__getitem__", native_str_getitem,
                                         L"Return self[index]."),
                resolve_trusted_str_getitem_handler),
            builtin_intrinsic_method(L"lower", native_str_lower,
                                     L"Return a lowercase copy."),
            builtin_intrinsic_method(L"capitalize", native_str_capitalize,
                                     L"Return a capitalized copy."),
            builtin_intrinsic_method(L"swapcase", native_str_swapcase,
                                     L"Return a case-swapped copy."),
            builtin_intrinsic_method(L"upper", native_str_upper,
                                     L"Return an uppercase copy."),
            with_keyword_parameter_names(
                with_defaults(builtin_intrinsic_method(
                                  L"startswith", native_str_startswith,
                                  L"Return whether self starts with prefix."),
                              str_start_end_defaults.value()),
                prefix_start_end_names, 3, 1),
            with_keyword_parameter_names(
                with_defaults(builtin_intrinsic_method(
                                  L"endswith", native_str_endswith,
                                  L"Return whether self ends with suffix."),
                              str_start_end_defaults.value()),
                suffix_start_end_names, 3, 1),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__contains__", native_str_contains,
                    L"Return whether needle is a substring of self."),
                resolve_trusted_str_contains_handler),
            with_keyword_parameter_names(
                with_defaults(builtin_intrinsic_method(
                                  L"find", native_str_find,
                                  L"Return first substring index or -1."),
                              str_start_end_defaults.value()),
                sub_start_end_names, 3, 1),
            with_keyword_parameter_names(
                with_defaults(builtin_intrinsic_method(
                                  L"rfind", native_str_rfind,
                                  L"Return last substring index or -1."),
                              str_start_end_defaults.value()),
                sub_start_end_names, 3, 1),
            with_keyword_parameter_names(
                with_defaults(
                    builtin_intrinsic_method(L"index", native_str_index,
                                             L"Return first substring index."),
                    str_start_end_defaults.value()),
                sub_start_end_names, 3, 1),
            with_keyword_parameter_names(
                with_defaults(
                    builtin_intrinsic_method(L"rindex", native_str_rindex,
                                             L"Return last substring index."),
                    str_start_end_defaults.value()),
                sub_start_end_names, 3, 1),
            with_keyword_parameter_names(
                with_defaults(builtin_intrinsic_method(
                                  L"count", native_str_count,
                                  L"Return number of substring occurrences."),
                              str_start_end_defaults.value()),
                sub_start_end_names, 3, 1),
            with_keyword_parameter_names(
                builtin_intrinsic_method(L"removeprefix",
                                         native_str_removeprefix,
                                         L"Return a copy with prefix removed."),
                prefix_names, 1, 1),
            with_keyword_parameter_names(
                builtin_intrinsic_method(L"removesuffix",
                                         native_str_removesuffix,
                                         L"Return a copy with suffix removed."),
                suffix_names, 1, 1),
            with_keyword_parameter_names(
                with_defaults(builtin_intrinsic_method(
                                  L"split", native_str_split,
                                  L"Return a list of split substrings."),
                              str_split_defaults.value()),
                sep_maxsplit_names, 2, 1),
            with_keyword_parameter_names(
                with_defaults(builtin_intrinsic_method(
                                  L"rsplit", native_str_rsplit,
                                  L"Return a list of split substrings."),
                              str_split_defaults.value()),
                sep_maxsplit_names, 2, 1),
            with_keyword_parameter_names(
                builtin_intrinsic_method(L"partition", native_str_partition,
                                         L"Partition at separator."),
                sep_names, 1, 1),
            with_keyword_parameter_names(
                builtin_intrinsic_method(L"rpartition", native_str_rpartition,
                                         L"Partition at last separator."),
                sep_names, 1, 1),
            with_keyword_parameter_names(
                with_defaults(builtin_intrinsic_method(
                                  L"replace", native_str_replace,
                                  L"Return a copy with replacements."),
                              str_replace_defaults.value()),
                replace_names, 3, 1),
            with_defaults(
                with_keyword_parameter_names(
                    builtin_intrinsic_method(L"strip", native_str_strip,
                                             L"Return a stripped copy."),
                    chars_names, 1, 1),
                str_strip_defaults.value()),
            with_defaults(
                with_keyword_parameter_names(
                    builtin_intrinsic_method(L"lstrip", native_str_lstrip,
                                             L"Return a left-stripped copy."),
                    chars_names, 1, 1),
                str_strip_defaults.value()),
            with_defaults(
                with_keyword_parameter_names(
                    builtin_intrinsic_method(L"rstrip", native_str_rstrip,
                                             L"Return a right-stripped copy."),
                    chars_names, 1, 1),
                str_strip_defaults.value()),
            builtin_intrinsic_method(L"join", native_str_join,
                                     L"Join list or tuple of strings."),
            builtin_intrinsic_method(
                L"isalpha", native_str_isalpha,
                L"Return whether all chars are alphabetic."),
            builtin_intrinsic_method(L"isascii", native_str_isascii,
                                     L"Return whether all chars are ASCII."),
            builtin_intrinsic_method(L"isdigit", native_str_isdigit,
                                     L"Return whether all chars are digits."),
            builtin_intrinsic_method(
                L"isalnum", native_str_isalnum,
                L"Return whether all chars are alphanumeric."),
            builtin_intrinsic_method(
                L"islower", native_str_islower,
                L"Return whether all cased chars are lowercase."),
            builtin_intrinsic_method(
                L"isprintable", native_str_isprintable,
                L"Return whether all chars are printable."),
            builtin_intrinsic_method(
                L"isspace", native_str_isspace,
                L"Return whether all chars are whitespace."),
            builtin_intrinsic_method(
                L"isupper", native_str_isupper,
                L"Return whether all cased chars are uppercase."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->str_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

    uint64_t string_hash(TValue<String> s)
    {
        String *str = s.extract();
        uint64_t len = str->count.extract();

        cl_wchar *c = &str->data[0];
        uint64_t hash = 5381;
        for(uint64_t i = 0; i < len; ++i)
        {
            hash = hash * 33 + c[i];
        }
        return hash;
    }

    TValue<SMI> string_hash_normalized(TValue<String> s)
    {
        return canonicalize_nonnegative_raw_hash(string_hash(s));
    }

    const cl_wchar *string_as_wchar_t(TValue<String> s)
    {
        String *str = s.extract();
        cl_wchar *c = &str->data[0];
        return c;
    }

    std::wstring_view string_view(TValue<String> s)
    {
        String *str = s.extract();
        return std::wstring_view(str->data, size_t(str->count.extract()));
    }

    std::optional<TValue<String>>
    try_make_string_from_utf8(ThreadState *thread, std::string_view bytes)
    {
        std::optional<unicode::Utf8WcharLayout> layout =
            unicode::validate_utf8_for_wchar(bytes);
        if(!layout.has_value())
        {
            return std::nullopt;
        }

        String *string = thread->make_object_raw<String>(TValue<SMI>::from_smi(
            static_cast<int64_t>(layout->code_unit_count)));
        if(!unicode::decode_utf8_into_wchar(bytes, string->data,
                                            layout->code_unit_count))
        {
            return std::nullopt;
        }
        string->data[layout->code_unit_count] = 0;
        return TValue<String>::from_oop(string);
    }

    bool string_eq_slow_path(TValue<String> a, TValue<String> b)
    {

        const String *sa = a.extract();
        const String *sb = b.extract();

        if(sa->count != sb->count)
            return false;

        uint64_t len = sa->count.extract();

        for(uint64_t i = 0; i < len; ++i)
        {
            if(sa->data[i] != sb->data[i])
                return false;
        }
        return true;
    }

    int string_compare(TValue<String> a, TValue<String> b)
    {
        if(a.raw_value().as.integer == b.raw_value().as.integer)
        {
            return 0;
        }

        const String *sa = a.extract();
        const String *sb = b.extract();
        uint64_t a_len = sa->count.extract();
        uint64_t b_len = sb->count.extract();
        uint64_t min_len = std::min(a_len, b_len);
        for(uint64_t i = 0; i < min_len; ++i)
        {
            if(sa->data[i] < sb->data[i])
            {
                return -1;
            }
            if(sa->data[i] > sb->data[i])
            {
                return 1;
            }
        }
        if(a_len < b_len)
        {
            return -1;
        }
        if(a_len > b_len)
        {
            return 1;
        }
        return 0;
    }

}  // namespace cl
