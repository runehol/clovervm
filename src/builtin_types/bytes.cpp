#include "builtin_types/bytes.h"

#include "builtin_types/hash.h"
#include "builtin_types/int.h"
#include "builtin_types/list.h"
#include "builtin_types/slice.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <algorithm>
#include <cassert>
#include <compare>
#include <string>
#include <vector>

namespace cl
{
    static Value require_bytes_receiver(Value self, const wchar_t *method_name)
    {
        if(!can_convert_to<Bytes>(self))
        {
            std::wstring message = L"bytes.";
            message += method_name;
            message += L" expects a bytes receiver";
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message.c_str());
        }
        return Value::None();
    }

    static Value require_bytes_argument(Value value, const wchar_t *message)
    {
        if(!can_convert_to<Bytes>(value))
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError", message);
        }
        return Value::None();
    }

    static Expected<uint8_t> value_to_byte(Value value)
    {
        if(!is_exact_int_value(value))
        {
            return Expected<uint8_t>::raise_exception(
                L"TypeError", L"bytes item must be an integer");
        }

        SmiViewStorage value_storage;
        SmiViewStorage lower_storage;
        SmiViewStorage upper_storage;
        ConstBigIntView value_view =
            exact_int_value_bigint_view(value, &value_storage);
        ConstBigIntView lower = smi_bigint_view(0, &lower_storage);
        ConstBigIntView upper = smi_bigint_view(255, &upper_storage);
        if(compare_bigint(value_view, lower) < 0 ||
           compare_bigint(value_view, upper) > 0)
        {
            return Expected<uint8_t>::raise_exception(
                L"ValueError", L"bytes must be in range(0, 256)");
        }

        TValue<SMI> smi = TValue<SMI>::from_smi(0);
        Expected<IntToSmiStatus> status =
            try_exact_int_value_to_smi(value, &smi);
        if(status.has_exception())
        {
            return Expected<uint8_t>::propagate_exception();
        }
        assert(status.value() == IntToSmiStatus::Converted);
        int64_t raw = smi.extract();
        return Expected<uint8_t>::ok(static_cast<uint8_t>(raw));
    }

    static Expected<std::vector<uint8_t>> bytes_from_list(List *list)
    {
        std::vector<uint8_t> result;
        result.reserve(list->size());
        for(size_t idx = 0; idx < list->size(); ++idx)
        {
            result.push_back(CL_TRY(value_to_byte(list->item_unchecked(idx))));
        }
        return Expected<std::vector<uint8_t>>::ok(std::move(result));
    }

    static Expected<std::vector<uint8_t>> bytes_from_tuple(Tuple *tuple)
    {
        std::vector<uint8_t> result;
        result.reserve(tuple->size());
        for(size_t idx = 0; idx < tuple->size(); ++idx)
        {
            result.push_back(CL_TRY(value_to_byte(tuple->item_unchecked(idx))));
        }
        return Expected<std::vector<uint8_t>>::ok(std::move(result));
    }

    static Value native_bytes_new(ThreadState *thread, Value cls_value,
                                  Value source)
    {
        if(cls_value != Value::from_oop(active_vm()->bytes_class()))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"bytes.__new__ expects bytes as cls");
        }
        if(source.is_none())
        {
            return thread->make_object_value<Bytes>(std::span<const uint8_t>{})
                .raw_value();
        }
        if(can_convert_to<Bytes>(source))
        {
            return source;
        }
        if(can_convert_to<List>(source))
        {
            std::vector<uint8_t> bytes =
                CL_TRY(bytes_from_list(source.get_ptr<List>()));
            return thread
                ->make_object_value<Bytes>(std::span<const uint8_t>(bytes))
                .raw_value();
        }
        if(can_convert_to<Tuple>(source))
        {
            std::vector<uint8_t> bytes =
                CL_TRY(bytes_from_tuple(source.get_ptr<Tuple>()));
            return thread
                ->make_object_value<Bytes>(std::span<const uint8_t>(bytes))
                .raw_value();
        }
        return thread->set_pending_builtin_exception_string(
            L"TypeError", L"cannot convert object to bytes");
    }

    static Value native_bytes_repr(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"__repr__"));
        return bytes_repr(thread, TValue<Bytes>::from_value_assumed(self))
            .raw_value();
    }

    static Value native_bytes_len(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"__len__"));
        return self.get_ptr<Bytes>()->count.raw_value();
    }

    static Value native_bytes_hash(ThreadState *thread, Value self)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"__hash__"));
        return bytes_hash_normalized(TValue<Bytes>::from_value_assumed(self))
            .raw_value();
    }

    static Value native_bytes_add(ThreadState *thread, Value left_value,
                                  Value right_value)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(left_value, L"__add__"));
        if(!can_convert_to<Bytes>(right_value))
        {
            return Value::NotImplemented();
        }
        return left_value.get_ptr<Bytes>()
            ->concat(right_value.get_ptr<Bytes>())
            .raw_value();
    }

    struct BytesEqOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"bytes.__eq__ expects a bytes receiver";

        Value operator()(ThreadState *thread, TValue<Bytes> left,
                         TValue<Bytes> right) const
        {
            (void)thread;
            return bytes_eq(left, right) ? Value::True() : Value::False();
        }
    };

    struct BytesNeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"bytes.__ne__ expects a bytes receiver";

        Value operator()(ThreadState *thread, TValue<Bytes> left,
                         TValue<Bytes> right) const
        {
            (void)thread;
            return bytes_eq(left, right) ? Value::False() : Value::True();
        }
    };

    struct BytesLtOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"bytes.__lt__ expects a bytes receiver";

        Value operator()(ThreadState *thread, TValue<Bytes> left,
                         TValue<Bytes> right) const
        {
            (void)thread;
            return bytes_compare(left, right) < 0 ? Value::True()
                                                  : Value::False();
        }
    };

    struct BytesLeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"bytes.__le__ expects a bytes receiver";

        Value operator()(ThreadState *thread, TValue<Bytes> left,
                         TValue<Bytes> right) const
        {
            (void)thread;
            return bytes_compare(left, right) <= 0 ? Value::True()
                                                   : Value::False();
        }
    };

    struct BytesGtOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"bytes.__gt__ expects a bytes receiver";

        Value operator()(ThreadState *thread, TValue<Bytes> left,
                         TValue<Bytes> right) const
        {
            (void)thread;
            return bytes_compare(left, right) > 0 ? Value::True()
                                                  : Value::False();
        }
    };

    struct BytesGeOperator
    {
        static constexpr const wchar_t *receiver_error =
            L"bytes.__ge__ expects a bytes receiver";

        Value operator()(ThreadState *thread, TValue<Bytes> left,
                         TValue<Bytes> right) const
        {
            (void)thread;
            return bytes_compare(left, right) >= 0 ? Value::True()
                                                   : Value::False();
        }
    };

    struct BytesAddOperator
    {
        Value operator()(ThreadState *thread, TValue<Bytes> left,
                         TValue<Bytes> right) const
        {
            (void)thread;
            return left.extract()->concat(right.extract()).raw_value();
        }
    };

    template <typename Operator>
    static Value native_bytes_compare_operator(ThreadState *thread, Value self,
                                               Value other)
    {
        if(!can_convert_to<Bytes>(self))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", Operator::receiver_error);
        }
        if(!can_convert_to<Bytes>(other))
        {
            return Value::NotImplemented();
        }
        return Operator{}(thread, TValue<Bytes>::from_value_assumed(self),
                          TValue<Bytes>::from_value_assumed(other));
    }

    template <typename Operator>
    static Value trusted_bytes_bytes_operator(ThreadState *thread,
                                              Value left_value,
                                              Value right_value)
    {
        return Operator{}(thread, TValue<Bytes>::from_value_assumed(left_value),
                          TValue<Bytes>::from_value_assumed(right_value));
    }

    template <typename Operator>
    static TrustedResolution resolve_trusted_bytes_bytes_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key)
    {
        if(vm->shape_for_key(operand0_key)->get_class() == vm->bytes_class() &&
           vm->shape_for_key(operand1_key)->get_class() == vm->bytes_class())
        {
            return TrustedResolution::call_trusted(
                trusted_bytes_bytes_operator<Operator>);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    template <typename NormalOperator, typename ReflectedOperator>
    static TrustedResolution resolve_trusted_bytes_bytes_resolver(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(order == TrustedHandlerOperandOrder::Reflected)
        {
            return resolve_trusted_bytes_bytes_handler<ReflectedOperator>(
                vm, operand0_key, operand1_key);
        }
        return resolve_trusted_bytes_bytes_handler<NormalOperator>(
            vm, operand0_key, operand1_key);
    }

    static Value trusted_bytes_hash(ThreadState *thread, Value value)
    {
        (void)thread;
        return bytes_hash_normalized(TValue<Bytes>::from_value_assumed(value))
            .raw_value();
    }

    static TrustedResolution resolve_trusted_bytes_hash_handler(
        VirtualMachine *vm, ShapeKey operand0_key, ShapeKey operand1_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        (void)operand1_key;
        (void)order;
        if(requested_arity != TrustedHandlerArity::Unary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(operand0_key)->get_class() == vm->bytes_class())
        {
            return TrustedResolution::call_trusted(trusted_bytes_hash);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static Value native_bytes_getitem(ThreadState *thread, Value self,
                                      Value index_value)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"__getitem__"));
        if(can_convert_to<Slice>(index_value))
        {
            TValue<Slice> slice =
                TValue<Slice>::from_value_assumed(index_value);
            if(slice.extract()->step.raw_value().is_none())
            {
                NormalizedNonstridedSlice normalized =
                    CL_TRY(normalize_nonstrided_slice_for_length(
                        thread, slice, self.get_ptr<Bytes>()->count.extract()));
                return self.get_ptr<Bytes>()
                    ->get_slice(thread, normalized)
                    .raw_value();
            }
            NormalizedGeneralSlice normalized =
                CL_TRY(normalize_general_slice_for_length(
                    thread, slice, self.get_ptr<Bytes>()->count.extract()));
            return self.get_ptr<Bytes>()
                ->get_slice(thread, normalized)
                .raw_value();
        }

        TValue<SMI> index = TValue<SMI>::from_smi(0);
        Expected<IntToSmiStatus> status =
            try_exact_int_value_to_smi(index_value, &index);
        if(status.has_exception())
        {
            return Value::exception_marker();
        }
        if(status.value() == IntToSmiStatus::NotInt)
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"bytes indices must be integers or slices");
        }
        return self.get_ptr<Bytes>()->byte_at(thread, index.extract());
    }

    static Value trusted_bytes_getitem_smi_handler(ThreadState *thread,
                                                   Value self,
                                                   Value index_value)
    {
        return self.get_ptr<Bytes>()->byte_at(thread, index_value.get_smi());
    }

    static Value trusted_bytes_getitem_nonstrided_slice_handler(
        ThreadState *thread, Value self, Value index_value)
    {
        TValue<Slice> slice = TValue<Slice>::from_value_assumed(index_value);
        NormalizedNonstridedSlice normalized =
            CL_TRY(normalize_nonstrided_slice_for_length(
                thread, slice,
                static_cast<int64_t>(self.get_ptr<Bytes>()->count.extract())));
        return self.get_ptr<Bytes>()->get_slice(thread, normalized).raw_value();
    }

    static Value
    trusted_bytes_getitem_general_slice_handler(ThreadState *thread, Value self,
                                                Value index_value)
    {
        TValue<Slice> slice = TValue<Slice>::from_value_assumed(index_value);
        NormalizedGeneralSlice normalized =
            CL_TRY(normalize_general_slice_for_length(
                thread, slice,
                static_cast<int64_t>(self.get_ptr<Bytes>()->count.extract())));
        return self.get_ptr<Bytes>()->get_slice(thread, normalized).raw_value();
    }

    static TrustedResolution resolve_trusted_bytes_getitem_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(container_key)->get_class() != vm->bytes_class())
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(key_key == ShapeKey::from_value(Value::from_smi(0)))
        {
            return TrustedResolution::call_trusted(
                trusted_bytes_getitem_smi_handler);
        }
        if(key_key == ShapeKey::from_shape(vm->slice_step_none_shape()))
        {
            return TrustedResolution::call_trusted(
                trusted_bytes_getitem_nonstrided_slice_handler);
        }
        if(key_key == ShapeKey::from_shape(vm->slice_general_shape()))
        {
            return TrustedResolution::call_trusted(
                trusted_bytes_getitem_general_slice_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static Expected<uint8_t> contains_needle_as_byte(Value needle)
    {
        TValue<SMI> smi = TValue<SMI>::from_smi(0);
        Expected<IntToSmiStatus> status =
            try_exact_int_value_to_smi(needle, &smi);
        if(status.has_exception())
        {
            return Expected<uint8_t>::propagate_exception();
        }
        if(status.value() == IntToSmiStatus::NotInt)
        {
            return Expected<uint8_t>::raise_exception(
                L"TypeError",
                L"a bytes-like object or integer is required, not other");
        }
        int64_t raw = smi.extract();
        if(raw < 0 || raw > 255)
        {
            return Expected<uint8_t>::raise_exception(
                L"ValueError", L"byte must be in range(0, 256)");
        }
        return Expected<uint8_t>::ok(static_cast<uint8_t>(raw));
    }

    static Expected<uint8_t> method_needle_as_byte(Value needle)
    {
        if(!is_intlike_value(needle))
        {
            return Expected<uint8_t>::raise_exception(
                L"TypeError", L"a bytes-like object is required, not other");
        }

        SmiViewStorage value_storage;
        SmiViewStorage lower_storage;
        SmiViewStorage upper_storage;
        ConstBigIntView value_view =
            intlike_value_bigint_view(needle, &value_storage);
        ConstBigIntView lower = smi_bigint_view(0, &lower_storage);
        ConstBigIntView upper = smi_bigint_view(255, &upper_storage);
        if(compare_bigint(value_view, lower) < 0 ||
           compare_bigint(value_view, upper) > 0)
        {
            return Expected<uint8_t>::raise_exception(
                L"ValueError", L"byte must be in range(0, 256)");
        }

        TValue<SMI> smi = TValue<SMI>::from_smi(0);
        Expected<IntToSmiStatus> status =
            try_intlike_value_to_smi(needle, &smi);
        if(status.has_exception())
        {
            return Expected<uint8_t>::propagate_exception();
        }
        assert(status.value() == IntToSmiStatus::Converted);
        return Expected<uint8_t>::ok(static_cast<uint8_t>(smi.extract()));
    }

    static Value native_bytes_contains(ThreadState *thread, Value self,
                                       Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"__contains__"));
        if(can_convert_to<Bytes>(needle_value))
        {
            return self.get_ptr<Bytes>()->find(needle_value.get_ptr<Bytes>()) >=
                           0
                       ? Value::True()
                       : Value::False();
        }
        uint8_t byte = CL_TRY(contains_needle_as_byte(needle_value));
        return self.get_ptr<Bytes>()->contains_byte(byte) ? Value::True()
                                                          : Value::False();
    }

    static Value trusted_bytes_contains_handler(ThreadState *thread, Value self,
                                                Value needle_value)
    {
        return native_bytes_contains(thread, self, needle_value);
    }

    static TrustedResolution resolve_trusted_bytes_contains_handler(
        VirtualMachine *vm, ShapeKey container_key, ShapeKey key_key,
        TrustedHandlerOperandOrder order, TrustedHandlerArity requested_arity)
    {
        (void)key_key;
        assert(order == TrustedHandlerOperandOrder::Normal);
        if(requested_arity != TrustedHandlerArity::Binary)
        {
            return TrustedResolution::no_trusted_handler_call_untrusted();
        }
        if(vm->shape_for_key(container_key)->get_class() == vm->bytes_class())
        {
            return TrustedResolution::call_trusted(
                trusted_bytes_contains_handler);
        }
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    static Value native_bytes_startswith(ThreadState *thread, Value self,
                                         Value prefix_value)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"startswith"));
        CL_PROPAGATE_EXCEPTION(require_bytes_argument(
            prefix_value, L"startswith first arg must be bytes"));
        return self.get_ptr<Bytes>()->startswith(prefix_value.get_ptr<Bytes>())
                   ? Value::True()
                   : Value::False();
    }

    static Value native_bytes_endswith(ThreadState *thread, Value self,
                                       Value suffix_value)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"endswith"));
        CL_PROPAGATE_EXCEPTION(require_bytes_argument(
            suffix_value, L"endswith first arg must be bytes"));
        return self.get_ptr<Bytes>()->endswith(suffix_value.get_ptr<Bytes>())
                   ? Value::True()
                   : Value::False();
    }

    static Value native_bytes_find(ThreadState *thread, Value self,
                                   Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"find"));
        if(is_intlike_value(needle_value))
        {
            uint8_t needle = CL_TRY(method_needle_as_byte(needle_value));
            return Value::from_smi(self.get_ptr<Bytes>()->find_byte(needle));
        }
        CL_PROPAGATE_EXCEPTION(
            require_bytes_argument(needle_value, L"must be bytes, not other"));
        return Value::from_smi(
            self.get_ptr<Bytes>()->find(needle_value.get_ptr<Bytes>()));
    }

    static Value native_bytes_index(ThreadState *thread, Value self,
                                    Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"index"));
        if(is_intlike_value(needle_value))
        {
            uint8_t needle = CL_TRY(method_needle_as_byte(needle_value));
            return self.get_ptr<Bytes>()->index_byte(needle);
        }
        CL_PROPAGATE_EXCEPTION(
            require_bytes_argument(needle_value, L"must be bytes, not other"));
        return self.get_ptr<Bytes>()->index(needle_value.get_ptr<Bytes>());
    }

    static Value native_bytes_count(ThreadState *thread, Value self,
                                    Value needle_value)
    {
        CL_PROPAGATE_EXCEPTION(require_bytes_receiver(self, L"count"));
        if(is_intlike_value(needle_value))
        {
            uint8_t needle = CL_TRY(method_needle_as_byte(needle_value));
            return Value::from_smi(self.get_ptr<Bytes>()->count_byte(needle));
        }
        CL_PROPAGATE_EXCEPTION(
            require_bytes_argument(needle_value, L"must be bytes, not other"));
        return Value::from_smi(self.get_ptr<Bytes>()->count_subsequence(
            needle_value.get_ptr<Bytes>()));
    }

    Value Bytes::byte_at(ThreadState *thread, int64_t py_idx) const
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
                L"IndexError", L"index out of range");
        }
        return Value::from_smi(data[static_cast<size_t>(normalized)]);
    }

    TValue<Bytes> Bytes::get_slice(ThreadState *thread,
                                   const NormalizedNonstridedSlice &slice) const
    {
        return thread->make_object_value<Bytes>(
            std::span<const uint8_t>(&data[static_cast<size_t>(slice.start)],
                                     slice.selected_sequence_length));
    }

    TValue<Bytes> Bytes::get_slice(ThreadState *thread,
                                   const NormalizedGeneralSlice &slice) const
    {
        TValue<Bytes> result =
            thread->make_object_value<Bytes>(TValue<SMI>::from_smi(
                static_cast<int64_t>(slice.selected_sequence_length)));
        int64_t read_idx = slice.start;
        for(size_t write_idx = 0; write_idx < slice.selected_sequence_length;
            ++write_idx)
        {
            result.extract()->data[write_idx] =
                data[static_cast<size_t>(read_idx)];
            read_idx += slice.step;
        }
        return result;
    }

    TValue<Bytes> Bytes::concat(const Bytes *other) const
    {
        std::vector<uint8_t> result;
        result.reserve(size_t(count.extract()) +
                       size_t(other->count.extract()));
        result.insert(result.end(), data, data + size_t(count.extract()));
        result.insert(result.end(), other->data,
                      other->data + size_t(other->count.extract()));
        return active_thread()->make_object_value<Bytes>(
            std::span<const uint8_t>(result));
    }

    bool Bytes::startswith(const Bytes *prefix) const
    {
        std::span<const uint8_t> str(data, size_t(count.extract()));
        std::span<const uint8_t> prefix_view(prefix->data,
                                             size_t(prefix->count.extract()));
        return str.size() >= prefix_view.size() &&
               std::equal(prefix_view.begin(), prefix_view.end(), str.begin());
    }

    bool Bytes::endswith(const Bytes *suffix) const
    {
        std::span<const uint8_t> str(data, size_t(count.extract()));
        std::span<const uint8_t> suffix_view(suffix->data,
                                             size_t(suffix->count.extract()));
        return str.size() >= suffix_view.size() &&
               std::equal(suffix_view.begin(), suffix_view.end(),
                          str.end() - suffix_view.size());
    }

    int64_t Bytes::find(const Bytes *needle) const
    {
        std::span<const uint8_t> str(data, size_t(count.extract()));
        std::span<const uint8_t> needle_view(needle->data,
                                             size_t(needle->count.extract()));
        if(needle_view.empty())
        {
            return 0;
        }
        auto found = std::search(str.begin(), str.end(), needle_view.begin(),
                                 needle_view.end());
        if(found == str.end())
        {
            return -1;
        }
        return static_cast<int64_t>(found - str.begin());
    }

    int64_t Bytes::find_byte(uint8_t needle) const
    {
        std::span<const uint8_t> str(data, size_t(count.extract()));
        auto found = std::find(str.begin(), str.end(), needle);
        if(found == str.end())
        {
            return -1;
        }
        return static_cast<int64_t>(found - str.begin());
    }

    Value Bytes::index(const Bytes *needle) const
    {
        int64_t found = find(needle);
        if(found == -1)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"subsection not found");
        }
        return Value::from_smi(found);
    }

    Value Bytes::index_byte(uint8_t needle) const
    {
        int64_t found = find_byte(needle);
        if(found == -1)
        {
            return active_thread()->set_pending_builtin_exception_string(
                L"ValueError", L"subsection not found");
        }
        return Value::from_smi(found);
    }

    int64_t Bytes::count_subsequence(const Bytes *needle) const
    {
        std::span<const uint8_t> str(data, size_t(count.extract()));
        std::span<const uint8_t> needle_view(needle->data,
                                             size_t(needle->count.extract()));
        if(needle_view.empty())
        {
            return static_cast<int64_t>(str.size() + 1);
        }
        int64_t result = 0;
        auto pos = str.begin();
        while(pos != str.end())
        {
            auto found = std::search(pos, str.end(), needle_view.begin(),
                                     needle_view.end());
            if(found == str.end())
            {
                break;
            }
            ++result;
            pos = found + needle_view.size();
        }
        return result;
    }

    int64_t Bytes::count_byte(uint8_t needle) const
    {
        std::span<const uint8_t> str(data, size_t(count.extract()));
        return static_cast<int64_t>(std::count(str.begin(), str.end(), needle));
    }

    bool Bytes::contains_byte(uint8_t needle) const
    {
        std::span<const uint8_t> str(data, size_t(count.extract()));
        return std::find(str.begin(), str.end(), needle) != str.end();
    }

    std::span<const uint8_t> bytes_view(TValue<Bytes> value)
    {
        Bytes *bytes = value.extract();
        return std::span<const uint8_t>(bytes->data,
                                        size_t(bytes->count.extract()));
    }

    uint64_t bytes_hash(TValue<Bytes> value)
    {
        uint64_t hash = 5381;
        for(uint8_t byte: bytes_view(value))
        {
            hash = hash * 33 + byte;
        }
        return hash;
    }

    TValue<SMI> bytes_hash_normalized(TValue<Bytes> value)
    {
        return canonicalize_nonnegative_raw_hash(bytes_hash(value));
    }

    bool bytes_eq(TValue<Bytes> left, TValue<Bytes> right)
    {
        if(left.raw_value() == right.raw_value())
        {
            return true;
        }
        std::span<const uint8_t> left_view = bytes_view(left);
        std::span<const uint8_t> right_view = bytes_view(right);
        return left_view.size() == right_view.size() &&
               std::equal(left_view.begin(), left_view.end(),
                          right_view.begin());
    }

    int bytes_compare(TValue<Bytes> left, TValue<Bytes> right)
    {
        if(left.raw_value() == right.raw_value())
        {
            return 0;
        }
        std::span<const uint8_t> left_view = bytes_view(left);
        std::span<const uint8_t> right_view = bytes_view(right);
        auto result = std::lexicographical_compare_three_way(
            left_view.begin(), left_view.end(), right_view.begin(),
            right_view.end());
        if(result < 0)
        {
            return -1;
        }
        if(result > 0)
        {
            return 1;
        }
        return 0;
    }

    static void append_hex_byte(StringBuilder &builder, uint8_t byte)
    {
        static constexpr wchar_t digits[] = L"0123456789abcdef";
        builder.append_c_str(L"\\x");
        builder.append_char(digits[byte >> 4]);
        builder.append_char(digits[byte & 0x0f]);
    }

    TValue<String> bytes_repr(ThreadState *thread, TValue<Bytes> value)
    {
        (void)thread;
        StringBuilder builder;
        builder.append_c_str(L"b'");
        for(uint8_t byte: bytes_view(value))
        {
            switch(byte)
            {
                case '\\':
                    builder.append_c_str(L"\\\\");
                    break;
                case '\'':
                    builder.append_c_str(L"\\'");
                    break;
                case '\t':
                    builder.append_c_str(L"\\t");
                    break;
                case '\n':
                    builder.append_c_str(L"\\n");
                    break;
                case '\r':
                    builder.append_c_str(L"\\r");
                    break;
                default:
                    if(byte >= 0x20 && byte <= 0x7e)
                    {
                        builder.append_char(static_cast<wchar_t>(byte));
                    }
                    else
                    {
                        append_hex_byte(builder, byte);
                    }
                    break;
            }
        }
        builder.append_char(L'\'');
        return TValue<String>::from_value_assumed(builder.finish());
    }

    BuiltinClassDefinition make_bytes_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Bytes};
        ClassObject *cls = ClassObject::make_builtin_class<Bytes>(
            vm->get_or_create_interned_string_value(L"bytes"),
            Bytes::native_static_release_count(), nullptr, 0,
            vm->object_class());
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_bytes_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> bytes_new_defaults(
            active_thread()->make_object_value<Tuple>(1));
        bytes_new_defaults.extract()->initialize_item_unchecked(0,
                                                                Value::None());
        BuiltinIntrinsicMethod methods[] = {
            with_defaults(builtin_intrinsic_method(L"__new__", native_bytes_new,
                                                   L"Create a bytes object."),
                          bytes_new_defaults.value()),
            builtin_intrinsic_method(L"__str__", native_bytes_repr,
                                     L"Return str(self)."),
            builtin_intrinsic_method(L"__repr__", native_bytes_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"__len__", native_bytes_len,
                                     L"Return len(self)."),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__hash__", native_bytes_hash,
                                         L"Return hash(self)."),
                resolve_trusted_bytes_hash_handler),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__add__", native_bytes_add,
                                         L"Return self + value."),
                resolve_trusted_bytes_bytes_resolver<BytesAddOperator,
                                                     BytesAddOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__eq__", native_bytes_compare_operator<BytesEqOperator>,
                    L"Return self == value."),
                resolve_trusted_bytes_bytes_resolver<BytesEqOperator,
                                                     BytesEqOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__ne__", native_bytes_compare_operator<BytesNeOperator>,
                    L"Return self != value."),
                resolve_trusted_bytes_bytes_resolver<BytesNeOperator,
                                                     BytesNeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__lt__", native_bytes_compare_operator<BytesLtOperator>,
                    L"Return self < value."),
                resolve_trusted_bytes_bytes_resolver<BytesLtOperator,
                                                     BytesGtOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__le__", native_bytes_compare_operator<BytesLeOperator>,
                    L"Return self <= value."),
                resolve_trusted_bytes_bytes_resolver<BytesLeOperator,
                                                     BytesGeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__gt__", native_bytes_compare_operator<BytesGtOperator>,
                    L"Return self > value."),
                resolve_trusted_bytes_bytes_resolver<BytesGtOperator,
                                                     BytesLtOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(
                    L"__ge__", native_bytes_compare_operator<BytesGeOperator>,
                    L"Return self >= value."),
                resolve_trusted_bytes_bytes_resolver<BytesGeOperator,
                                                     BytesLeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__getitem__", native_bytes_getitem,
                                         L"Return self[index]."),
                resolve_trusted_bytes_getitem_handler),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__contains__", native_bytes_contains,
                                         L"Return whether needle is in self."),
                resolve_trusted_bytes_contains_handler),
            builtin_intrinsic_method(
                L"startswith", native_bytes_startswith,
                L"Return whether self starts with prefix."),
            builtin_intrinsic_method(L"endswith", native_bytes_endswith,
                                     L"Return whether self ends with suffix."),
            builtin_intrinsic_method(L"find", native_bytes_find,
                                     L"Return first subsequence index or -1."),
            builtin_intrinsic_method(L"index", native_bytes_index,
                                     L"Return first subsequence index."),
            builtin_intrinsic_method(
                L"count", native_bytes_count,
                L"Return number of subsequence occurrences."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->bytes_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
