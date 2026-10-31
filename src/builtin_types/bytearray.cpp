#include "builtin_types/bytearray.h"
#include "builtin_types/binary_split.h"
#include "builtin_types/bytes.h"
#include "builtin_types/float.h"
#include "builtin_types/int.h"
#include "builtin_types/list.h"
#include "builtin_types/module_object.h"
#include "builtin_types/search_range.h"
#include "builtin_types/slice.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <algorithm>
#include <compare>
#include <string>

namespace cl
{
    static Expected<TValue<ByteArray>> receiver(Value self,
                                                const wchar_t *method)
    {
        if(!has_native_layout<ByteArray>(self))
        {
            std::wstring message = L"bytearray.";
            message += method;
            message += L" expects a bytearray receiver";
            return Expected<TValue<ByteArray>>::raise_exception(
                L"TypeError", message.c_str());
        }
        return Expected<TValue<ByteArray>>::ok(
            TValue<ByteArray>::from_value_assumed(self));
    }

    static Expected<size_t> checked_size(size_t count)
    {
        // RawArray growth doubles capacity up to the SMI limit. uint8_t storage
        // plus header, heap alignment and slab offset fit size_t in that range.
        static_assert(static_cast<uint64_t>(value_smi_max) < SIZE_MAX / 2);
        if(count > static_cast<size_t>(value_smi_max))
            return Expected<size_t>::raise_exception(
                L"OverflowError", L"bytearray size exceeds storage limit");
        return Expected<size_t>::ok(count);
    }

    static Expected<uint8_t> byte_value(Value item)
    {
        if(item.is_smi())
        {
            int64_t byte = item.get_smi();
            if(byte < 0 || byte > 255)
                return Expected<uint8_t>::raise_exception(
                    L"ValueError", L"byte must be in range(0, 256)");
            return Expected<uint8_t>::ok(static_cast<uint8_t>(byte));
        }
        if(!is_intlike_value(item))
            return Expected<uint8_t>::raise_exception(
                L"TypeError", L"byte must be an integer");
        SmiViewStorage storage, lower_storage, upper_storage;
        auto view = intlike_value_bigint_view(item, &storage);
        if(compare_bigint(view, smi_bigint_view(0, &lower_storage)) < 0 ||
           compare_bigint(view, smi_bigint_view(255, &upper_storage)) > 0)
            return Expected<uint8_t>::raise_exception(
                L"ValueError", L"byte must be in range(0, 256)");
        return Expected<uint8_t>::ok(
            static_cast<uint8_t>(CL_TRY(bigint_to_int64(view))));
    }

    // Saturation preserves slice clipping and distinguishes oversized indexed
    // access from supported positions. No user __index__ calls are made here.
    static Expected<int64_t> slice_index_clamped(Value value)
    {
        if(value.is_smi())
            return Expected<int64_t>::ok(value.get_smi());
        if(!is_intlike_value(value))
            return Expected<int64_t>::raise_exception(
                L"TypeError", L"bytearray indices must be integers or slices");
        SmiViewStorage storage, bound_storage;
        auto view = intlike_value_bigint_view(value, &storage);
        if(compare_bigint(view,
                          smi_bigint_view(value_smi_max, &bound_storage)) > 0)
            return Expected<int64_t>::ok(value_smi_max + 1);
        if(compare_bigint(view,
                          smi_bigint_view(-value_smi_max, &bound_storage)) < 0)
            return Expected<int64_t>::ok(-value_smi_max - 1);
        return bigint_to_int64(view);
    }

    static Expected<int64_t> item_index_or_raise(Value index)
    {
        if(index.is_smi())
            return Expected<int64_t>::ok(index.get_smi());
        if(!is_intlike_value(index))
            return Expected<int64_t>::raise_exception(
                L"TypeError", L"bytearray indices must be integers or slices");
        SmiViewStorage storage;
        auto converted =
            bigint_to_int64(intlike_value_bigint_view(index, &storage));
        if(converted.has_exception())
        {
            // Indexing uses IndexError for a value outside Py_ssize_t, before
            // byte conversion for assignment. Handle that conversion locally.
            active_thread()->clear_pending_exception();
            return Expected<int64_t>::raise_exception(
                L"IndexError",
                L"cannot fit integer into an index-sized integer");
        }
        return converted;
    }

    static Expected<size_t> position(ByteArray *array, int64_t pos)
    {
        if(pos < 0)
            pos += static_cast<int64_t>(array->size());
        if(pos < 0 || static_cast<uint64_t>(pos) >= array->size())
            return Expected<size_t>::raise_exception(
                L"IndexError", L"bytearray index out of range");
        return Expected<size_t>::ok(static_cast<size_t>(pos));
    }

    static Expected<NormalizedGeneralSlice> slice_indices(Value index,
                                                          size_t size)
    {
        auto slice = CL_TRY(TValue<Slice>::from_value_or_raise(
            index, L"TypeError", L"expected a slice"));
        int64_t step = slice.extract()->step.raw_value().is_none()
                           ? 1
                           : CL_TRY(slice_index_clamped(slice.extract()->step));
        if(step == 0)
            return Expected<NormalizedGeneralSlice>::raise_exception(
                L"ValueError", L"slice step cannot be zero");
        int64_t length = static_cast<int64_t>(size);
        int64_t lower = step < 0 ? -1 : 0;
        int64_t upper = step < 0 ? length - 1 : length;
        auto bound = [&](Value value,
                         int64_t default_value) -> Expected<int64_t> {
            if(value.is_none())
                return Expected<int64_t>::ok(default_value);
            int64_t pos = CL_TRY(slice_index_clamped(value));
            return Expected<int64_t>::ok(
                normalize_slice_field(pos, length, lower, upper));
        };
        int64_t start =
            CL_TRY(bound(slice.extract()->start, step < 0 ? length - 1 : 0));
        int64_t stop =
            CL_TRY(bound(slice.extract()->stop, step < 0 ? -1 : length));
        return Expected<NormalizedGeneralSlice>::ok(
            {start, stop, step,
             selected_sequence_length_for_general_slice(start, stop, step)});
    }

    static Value allocate(ThreadState *thread, Value cls)
    {
        if(cls != Value::from_oop(thread->get_machine()->bytearray_class()))
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"bytearray.__new__ expects bytearray as cls");
        return thread->make_object_value<ByteArray>().raw_value();
    }

    // Return False only when managed iteration is required. Strings must fail
    // here, including empty strings, rather than accidentally becoming empty.
    static Value initialize_native(ThreadState *thread, Value self,
                                   Value source)
    {
        auto array = CL_TRY(receiver(self, L"__new__"));
        if(is_intlike_value(source))
        {
            SmiViewStorage storage;
            int64_t count = CL_TRY(
                bigint_to_int64(intlike_value_bigint_view(source, &storage)));
            if(count < 0)
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"negative count");
            array.extract()->items.resize(
                CL_TRY(checked_size(static_cast<size_t>(count))));
            return Value::True();
        }
        std::span<const uint8_t> bytes;
        if(has_native_layout<Bytes>(source))
            bytes = bytes_view(TValue<Bytes>::from_value_assumed(source));
        else if(has_native_layout<ByteArray>(source))
            bytes = source.get_ptr<ByteArray>()->view();
        else if(has_native_layout<String>(source))
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"string argument without an encoding");
        else
            return Value::False();
        // Called only with a fresh, private destination, never self-aliasing.
        assert(self != source);
        array.extract()->items.resize(CL_TRY(checked_size(bytes.size())));
        std::copy(bytes.begin(), bytes.end(), array.extract()->items.data());
        return Value::True();
    }

    static Value copy_native(ThreadState *thread, Value self, Value source)
    {
        if(!has_native_layout<Bytes>(source) &&
           !has_native_layout<ByteArray>(source))
            return Value::False();
        return initialize_native(thread, self, source);
    }

    static Value native_len(ThreadState *, Value self)
    {
        return CL_TRY(receiver(self, L"__len__"))
            .extract()
            ->items.len()
            .raw_value();
    }

    static Value native_append(ThreadState *, Value self, Value item)
    {
        auto array = CL_TRY(receiver(self, L"append"));
        uint8_t byte = CL_TRY(byte_value(item));
        (void)CL_TRY(checked_size(array.extract()->size() + 1));
        array.extract()->items.push_back(byte);
        return Value::None();
    }

    static Value native_clear(ThreadState *, Value self)
    {
        CL_TRY(receiver(self, L"clear")).extract()->items.clear();
        return Value::None();
    }

    // Unlike item access, pop/insert report platform-index overflow as
    // OverflowError. Preserve the stored-integer-only bytearray boundary.
    static Expected<int64_t> method_index(Value index)
    {
        if(index.is_smi())
            return Expected<int64_t>::ok(index.get_smi());
        if(!is_intlike_value(index))
            return Expected<int64_t>::raise_exception(
                L"TypeError", L"index must be an integer");
        SmiViewStorage storage;
        return bigint_to_int64(intlike_value_bigint_view(index, &storage));
    }

    static Value native_insert(ThreadState *, Value self, Value index,
                               Value item)
    {
        auto array = CL_TRY(receiver(self, L"insert"));
        int64_t pos = CL_TRY(method_index(index));
        uint8_t byte = CL_TRY(byte_value(item));
        size_t old_size = array.extract()->size();
        if(pos < 0)
            pos = std::max<int64_t>(0, pos + static_cast<int64_t>(old_size));
        size_t offset = std::min(static_cast<size_t>(pos), old_size);
        (void)CL_TRY(checked_size(old_size + 1));
        array.extract()->items.push_back(byte);
        for(size_t n = old_size; n > offset; --n)
            array.extract()->items[n] = array.extract()->items[n - 1];
        array.extract()->items[offset] = byte;
        return Value::None();
    }

    static Value native_reverse(ThreadState *, Value self)
    {
        auto array = CL_TRY(receiver(self, L"reverse"));
        size_t size = array.extract()->size();
        for(size_t n = 0; n < size / 2; ++n)
            std::swap(array.extract()->items[n],
                      array.extract()->items[size - n - 1]);
        return Value::None();
    }

    static Value native_copy(ThreadState *thread, Value self)
    {
        Owned<TValue<ByteArray>> source(CL_TRY(receiver(self, L"copy")));
        Owned<TValue<ByteArray>> result(thread->make_object_value<ByteArray>());
        result.extract()->items.resize(source.extract()->size());
        // Acquire storage only after allocation; no Python callbacks or
        // safepoints occur during this copy.
        for(size_t n = 0; n < source.extract()->size(); ++n)
            result.extract()->items[n] = source.extract()->items[n];
        return result.raw_value();
    }

    static Value get_index(TValue<ByteArray> array, int64_t index)
    {
        return Value::from_smi(
            array.extract()->items[CL_TRY(position(array.extract(), index))]);
    }

    static Value get_slice(ThreadState *thread, TValue<ByteArray> array,
                           Value index)
    {
        auto slice = CL_TRY(slice_indices(index, array.extract()->size()));
        auto result = thread->make_object_value<ByteArray>();
        result.extract()->items.resize(slice.selected_sequence_length);
        int64_t pos = slice.start;
        for(size_t n = 0; n < slice.selected_sequence_length;
            ++n, pos += slice.step)
            result.extract()->items[n] =
                array.extract()->items[static_cast<size_t>(pos)];
        return result.raw_value();
    }

    static Value native_getitem(ThreadState *thread, Value self, Value index)
    {
        auto array = CL_TRY(receiver(self, L"__getitem__"));
        if(has_native_layout<Slice>(index))
            return get_slice(thread, array, index);
        return get_index(array, CL_TRY(item_index_or_raise(index)));
    }

    static Value validate_slice(ThreadState *thread, Value index, Value source)
    {
        (void)CL_TRY(slice_indices(index, 0));
        // Reject the supported numeric payloads before attempting iteration,
        // including subclasses with __iter__. This policy is specific to slice
        // assignment; constructor/extend still accept such iterables.
        if(is_intlike_value(source) || has_native_layout<Float>(source) ||
           has_native_layout<String>(source))
            return thread->set_pending_builtin_exception_string(
                L"TypeError",
                L"can assign only bytes or iterables of integers");
        return Value::None();
    }

    static Value set_index(TValue<ByteArray> array, int64_t raw_index,
                           Value value)
    {
        // A valid platform-sized index is converted before the byte, but
        // bounds are checked after the byte value, matching CPython.
        uint8_t byte = CL_TRY(byte_value(value));
        size_t pos = CL_TRY(position(array.extract(), raw_index));
        array.extract()->items[pos] = byte;
        return Value::None();
    }

    // The slice is normalized against this owner's current length. Shrinking
    // raw-byte storage cannot allocate or call Python.
    static void delete_slice(ByteArray *array,
                             const NormalizedGeneralSlice &slice)
    {
        size_t old_size = array->size();
        if(slice.selected_sequence_length == 0)
            return;
        // Traverse removed positions in ascending order, including
        // negative-step slices. All arithmetic stays in the SMI-sized sequence
        // range.
        int64_t first =
            slice.step > 0
                ? slice.start
                : slice.start +
                      static_cast<int64_t>(slice.selected_sequence_length - 1) *
                          slice.step;
        int64_t stride = slice.step > 0 ? slice.step : -slice.step;
        size_t remaining = slice.selected_sequence_length;
        size_t dest = 0;
        for(size_t pos = 0; pos < old_size; ++pos)
        {
            if(remaining && pos == static_cast<size_t>(first))
            {
                --remaining;
                if(remaining)
                    first += stride;
            }
            else
                array->items[dest++] = array->items[pos];
        }
        array->items.resize(dest);
    }

    static Value replace_slice(ThreadState *thread, TValue<ByteArray> array,
                               Value index, Value replacement)
    {
        assert(array.raw_value() != replacement);
        bool from_bytes = has_native_layout<Bytes>(replacement);
        assert(from_bytes || has_native_layout<ByteArray>(replacement));
        size_t count =
            from_bytes
                ? bytes_view(TValue<Bytes>::from_value_assumed(replacement))
                      .size()
                : replacement.get_ptr<ByteArray>()->size();
        // Acquire the source span only after destination growth. No span
        // crosses allocation, managed execution, or a safepoint.
        auto source_bytes = [&]() -> std::span<const uint8_t> {
            return from_bytes ? bytes_view(TValue<Bytes>::from_value_assumed(
                                    replacement))
                              : replacement.get_ptr<ByteArray>()->view();
        };
        auto slice = CL_TRY(slice_indices(index, array.extract()->size()));
        if(slice.step != 1)
        {
            if(count == 0)
            {
                delete_slice(array.extract(), slice);
                return Value::None();
            }
            if(count != slice.selected_sequence_length)
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"attempt to assign bytes of different size "
                                   L"to extended slice");
            int64_t pos = slice.start;
            for(size_t n = 0; n < count; ++n, pos += slice.step)
                array.extract()->items[static_cast<size_t>(pos)] =
                    source_bytes()[n];
            return Value::None();
        }
        size_t start = static_cast<size_t>(slice.start);
        size_t removed = slice.selected_sequence_length;
        size_t old_size = array.extract()->size();
        size_t new_size = CL_TRY(checked_size(old_size - removed + count));
        if(new_size > old_size)
        {
            if(new_size > array.extract()->items.capacity())
                array.extract()->items.reserve(detail::grown_capacity(
                    array.extract()->items.capacity(), new_size));
            array.extract()->items.resize(new_size);
            for(size_t pos = old_size; pos > start + removed; --pos)
                array.extract()->items[pos - 1 - removed + count] =
                    array.extract()->items[pos - 1];
        }
        else
        {
            for(size_t pos = start + removed; pos < old_size; ++pos)
                array.extract()->items[pos - removed + count] =
                    array.extract()->items[pos];
            array.extract()->items.resize(new_size);
        }
        for(size_t n = 0; n < count; ++n)
            array.extract()->items[start + n] = source_bytes()[n];
        return Value::None();
    }

    // True means a stored byte payload was appended; False requests managed
    // iterable conversion. No Python calls occur on the payload path.
    static Value extend_native(ThreadState *, Value self, Value source)
    {
        auto array = CL_TRY(receiver(self, L"extend"));
        bool from_bytes = has_native_layout<Bytes>(source);
        if(!from_bytes && !has_native_layout<ByteArray>(source))
            return Value::False();
        size_t old_size = array.extract()->size();
        // Capture the source length before resizing: source may be self.
        size_t count =
            from_bytes
                ? static_cast<size_t>(source.get_ptr<Bytes>()->count.extract())
                : source.get_ptr<ByteArray>()->size();
        if(count == 0)
            return Value::True();
        size_t new_size = CL_TRY(checked_size(old_size + count));
        if(new_size > array.extract()->items.capacity())
            array.extract()->items.reserve(detail::grown_capacity(
                array.extract()->items.capacity(), new_size));
        array.extract()->items.resize(new_size);
        // Obtain pointers only after growth. Self-extension copies the old
        // prefix to the disjoint new suffix, with no allocation or safepoint
        // between pointer acquisition and the completed copy.
        const uint8_t *data = from_bytes
                                  ? source.get_ptr<Bytes>()->data
                                  : source.get_ptr<ByteArray>()->items.data();
        std::copy_n(data, count, array.extract()->items.data() + old_size);
        return Value::True();
    }

    static Value set_slice(ThreadState *thread, TValue<ByteArray> array,
                           Value index, Value value)
    {
        CL_PROPAGATE_EXCEPTION(validate_slice(thread, index, value));
        if(array.raw_value() != value && (has_native_layout<Bytes>(value) ||
                                          has_native_layout<ByteArray>(value)))
            return replace_slice(thread, array, index, value);
        // Keep all managed inputs alive across iteration and possible
        // reclamation.
        Owned<TValue<ByteArray>> owner(array);
        Owned<Value> key(index);
        Owned<Value> source(value);
        auto snapshot = [&]() -> Value {
            if(array.raw_value() == value)
            {
                auto result = thread->make_object_value<ByteArray>();
                CL_PROPAGATE_EXCEPTION(initialize_native(
                    thread, result.raw_value(), source.value()));
                return result.raw_value();
            }
            return thread->call_clovervm_function(
                thread->get_machine()->bytearray_snapshot_function(),
                source.value());
        };
        Value snapshot_value = snapshot();
        CL_PROPAGATE_EXCEPTION(snapshot_value);
        Owned<TValue<ByteArray>> staged(
            TValue<ByteArray>::from_value_assumed(snapshot_value));
        // Iteration may have mutated the target: normalize against its current
        // length only now, with no retained storage pointer from before Python.
        return replace_slice(thread, owner.value(), key.value(),
                             staged.value().raw_value());
    }

    static Value native_setitem(ThreadState *thread, Value self, Value index,
                                Value value)
    {
        auto array = CL_TRY(receiver(self, L"__setitem__"));
        if(has_native_layout<Slice>(index))
            return set_slice(thread, array, index, value);
        return set_index(array, CL_TRY(item_index_or_raise(index)), value);
    }

    static Value del_index(TValue<ByteArray> array, int64_t index)
    {
        size_t old_size = array.extract()->size();
        size_t pos = CL_TRY(position(array.extract(), index));
        for(size_t n = pos + 1; n < old_size; ++n)
            array.extract()->items[n - 1] = array.extract()->items[n];
        array.extract()->items.resize(old_size - 1);
        return Value::None();
    }

    static Value native_pop(ThreadState *thread, Value self, Value index)
    {
        auto array = CL_TRY(receiver(self, L"pop"));
        int64_t pos = CL_TRY(method_index(index));
        size_t size = array.extract()->size();
        if(size == 0)
            return thread->set_pending_builtin_exception_string(
                L"IndexError", L"pop from empty bytearray");
        if(pos < 0)
            pos += static_cast<int64_t>(size);
        if(pos < 0 || static_cast<uint64_t>(pos) >= size)
            return thread->set_pending_builtin_exception_string(
                L"IndexError", L"pop index out of range");
        uint8_t byte = array.extract()->items[static_cast<size_t>(pos)];
        CL_PROPAGATE_EXCEPTION(del_index(array, pos));
        return Value::from_smi(byte);
    }

    static Value native_remove(ThreadState *thread, Value self, Value value)
    {
        auto array = CL_TRY(receiver(self, L"remove"));
        uint8_t byte = CL_TRY(byte_value(value));
        for(size_t n = 0; n < array.extract()->size(); ++n)
        {
            if(array.extract()->items[n] == byte)
                return del_index(array, static_cast<int64_t>(n));
        }
        return thread->set_pending_builtin_exception_string(
            L"ValueError", L"value not found in bytearray");
    }

    static Value del_slice(TValue<ByteArray> array, Value index)
    {
        auto slice = CL_TRY(slice_indices(index, array.extract()->size()));
        delete_slice(array.extract(), slice);
        return Value::None();
    }

    static Value native_delitem(ThreadState *, Value self, Value index)
    {
        auto array = CL_TRY(receiver(self, L"__delitem__"));
        if(has_native_layout<Slice>(index))
            return del_slice(array, index);
        return del_index(array, CL_TRY(item_index_or_raise(index)));
    }

    enum class ItemKeyKind
    {
        Smi,
        Slice,
    };

    template <ItemKeyKind KeyKind>
    static Value trusted_getitem(ThreadState *thread, Value self, Value index)
    {
        auto array = TValue<ByteArray>::from_value_assumed(self);
        if constexpr(KeyKind == ItemKeyKind::Slice)
            return get_slice(thread, array, index);
        else
            return get_index(array, index.get_smi());
    }

    template <ItemKeyKind KeyKind>
    static Value trusted_setitem(ThreadState *thread, Value self, Value index,
                                 Value value)
    {
        auto array = TValue<ByteArray>::from_value_assumed(self);
        if constexpr(KeyKind == ItemKeyKind::Slice)
            return set_slice(thread, array, index, value);
        else
            return set_index(array, index.get_smi(), value);
    }

    template <ItemKeyKind KeyKind>
    static Value trusted_delitem(ThreadState *, Value self, Value index)
    {
        auto array = TValue<ByteArray>::from_value_assumed(self);
        if constexpr(KeyKind == ItemKeyKind::Slice)
            return del_slice(array, index);
        else
            return del_index(array, index.get_smi());
    }

    template <auto SmiHandler, auto SliceHandler, TrustedHandlerArity Arity>
    static TrustedResolution
    resolve_item(VirtualMachine *vm, ShapeKey left, ShapeKey right,
                 TrustedHandlerOperandOrder order, TrustedHandlerArity arity)
    {
        if(order != TrustedHandlerOperandOrder::Normal || arity != Arity ||
           vm->shape_for_key(left)->get_class() != vm->bytearray_class())
            return TrustedResolution::no_trusted_handler_call_untrusted();
        if(right == ShapeKey::from_value(Value::from_smi(0)))
            return TrustedResolution::call_trusted(SmiHandler);
        if(right == ShapeKey::from_shape(vm->slice_step_none_shape()) ||
           right == ShapeKey::from_shape(vm->slice_general_shape()))
            return TrustedResolution::call_trusted(SliceHandler);
        return TrustedResolution::no_trusted_handler_call_untrusted();
    }

    // Callers prove a stored byte payload before entering this unchecked view.
    static std::span<const uint8_t> stored_byte_view(Value value)
    {
        if(has_native_layout<ByteArray>(value))
            return value.get_ptr<ByteArray>()->view();
        assert(has_native_layout<Bytes>(value));
        return bytes_view(TValue<Bytes>::from_value_assumed(value));
    }

    [[nodiscard]] static Value require_split_separator(Value separator)
    {
        if(has_native_layout<Bytes>(separator) ||
           has_native_layout<ByteArray>(separator))
            return Value::None();
        return active_thread()->set_pending_builtin_exception_string(
            L"TypeError", L"separator must be bytes or bytearray");
    }

    static std::string_view split_view(Value value)
    {
        auto view = stored_byte_view(value);
        return {reinterpret_cast<const char *>(view.data()), view.size()};
    }

    static TValue<ByteArray> copy_split_piece(TValue<ByteArray> source,
                                              BinarySplitPiece piece)
    {
        Owned<TValue<ByteArray>> owner(source);
        Owned<TValue<ByteArray>> result(
            active_thread()->make_object_value<ByteArray>());
        result.extract()->items.resize(piece.size);
        // Reacquire payloads after allocation; no callbacks, resizing of the
        // source, or safepoints occur while these pointers are in use.
        if(piece.size != 0)
            std::copy_n(owner.extract()->items.data() + piece.start, piece.size,
                        result.extract()->items.data());
        return result.value();
    }

    template <SearchDirection Direction>
    [[nodiscard]] static Value native_split(ThreadState *, Value self,
                                            Value separator, Value limit)
    {
        constexpr const wchar_t *method =
            Direction == SearchDirection::Forward ? L"split" : L"rsplit";
        Owned<TValue<ByteArray>> source(CL_TRY(receiver(self, method)));
        int64_t maxsplit = CL_TRY(binary_maxsplit(limit));
        Owned<Value> separator_owner(separator);
        if(!separator.is_none())
            CL_PROPAGATE_EXCEPTION(require_split_separator(separator));
        std::vector<BinarySplitPiece> pieces;
        {
            std::optional<std::string_view> sep;
            if(!separator.is_none())
            {
                sep = split_view(separator_owner.value());
                if(sep->empty())
                    return active_thread()
                        ->set_pending_builtin_exception_string(
                            L"ValueError", L"empty separator");
            }
            pieces = scan_binary_split(split_view(source.raw_value()), sep,
                                       maxsplit, Direction);
        }
        Owned<TValue<List>> result(active_thread()->make_object_value<List>());
        for(auto piece: pieces)
        {
            Owned<TValue<ByteArray>> part(
                copy_split_piece(source.value(), piece));
            result.extract()->append(part.raw_value());
        }
        return result.raw_value();
    }

    template <SearchDirection Direction>
    [[nodiscard]] static Value native_partition(ThreadState *, Value self,
                                                Value separator)
    {
        constexpr const wchar_t *method = Direction == SearchDirection::Forward
                                              ? L"partition"
                                              : L"rpartition";
        Owned<TValue<ByteArray>> source(CL_TRY(receiver(self, method)));
        CL_PROPAGATE_EXCEPTION(require_split_separator(separator));
        Owned<Value> separator_owner(separator);
        BinarySplitPiece pieces[3]{};
        {
            auto view = split_view(source.raw_value());
            auto sep = split_view(separator_owner.value());
            if(sep.empty())
                return active_thread()->set_pending_builtin_exception_string(
                    L"ValueError", L"empty separator");
            size_t found = sep.size() > view.size()
                               ? std::string_view::npos
                               : SeparatorSearch<char>(sep, Direction)
                                     .find(view, 0, view.size());
            if(found == std::string_view::npos)
            {
                size_t original = Direction == SearchDirection::Forward ? 0 : 2;
                pieces[original] = {0, view.size()};
            }
            else
            {
                pieces[0] = {0, found};
                pieces[1] = {found, sep.size()};
                pieces[2] = {found + sep.size(),
                             view.size() - found - sep.size()};
            }
        }
        Owned<TValue<Tuple>> result(
            active_thread()->make_object_value<Tuple>(3));
        for(size_t index = 0; index < 3; ++index)
        {
            Owned<TValue<ByteArray>> part(
                copy_split_piece(source.value(), pieces[index]));
            result.extract()->initialize_item_unchecked(index,
                                                        part.raw_value());
        }
        return result.raw_value();
    }

    enum class ByteArraySearch
    {
        Find,
        RFind,
        Index,
        RIndex,
        Count
    };

    // Pure synchronous scans: no allocation, callbacks, mutation, reclamation,
    // or safepoints while either stored payload is borrowed. Only scalars
    // escape.
    template <ByteArraySearch Operation>
    static int64_t search_bytes(std::span<const uint8_t> source,
                                std::span<const uint8_t> needle, size_t start,
                                size_t end)
    {
        constexpr bool counting = Operation == ByteArraySearch::Count;
        constexpr bool reverse = Operation == ByteArraySearch::RFind ||
                                 Operation == ByteArraySearch::RIndex;
        if(start > end)
            return counting ? 0 : -1;
        if(needle.empty())
            return static_cast<int64_t>(counting  ? end - start + 1
                                        : reverse ? end
                                                  : start);
        if(needle.size() > end - start)
            return counting ? 0 : -1;
        auto range = source.subspan(start, end - start);
        if constexpr(counting)
        {
            int64_t count = 0;
            while(range.size() >= needle.size())
            {
                auto found = std::search(range.begin(), range.end(),
                                         needle.begin(), needle.end());
                if(found == range.end())
                    break;
                ++count;
                range = range.subspan(
                    static_cast<size_t>(found - range.begin()) + needle.size());
            }
            return count;
        }
        else
        {
            auto found = reverse ? std::find_end(range.begin(), range.end(),
                                                 needle.begin(), needle.end())
                                 : std::search(range.begin(), range.end(),
                                               needle.begin(), needle.end());
            return found == range.end()
                       ? -1
                       : static_cast<int64_t>(start + (found - range.begin()));
        }
    }

    template <ByteArraySearch Operation>
    static Value native_search(ThreadState *thread, Value self, Value needle,
                               Value start_value, Value end_value)
    {
        constexpr const wchar_t *method =
            Operation == ByteArraySearch::Find     ? L"find"
            : Operation == ByteArraySearch::RFind  ? L"rfind"
            : Operation == ByteArraySearch::Index  ? L"index"
            : Operation == ByteArraySearch::RIndex ? L"rindex"
                                                   : L"count";
        Owned<TValue<ByteArray>> source(CL_TRY(receiver(self, method)));
        Owned<Value> needle_owner(needle);
        size_t start, end;
        CL_PROPAGATE_EXCEPTION(normalize_search_range(
            static_cast<int64_t>(source.extract()->size()), start_value,
            end_value, start, end));
        int64_t result;
        if(is_intlike_value(needle))
        {
            uint8_t byte = CL_TRY(byte_value(needle));
            result = search_bytes<Operation>(source.extract()->view(),
                                             {&byte, 1}, start, end);
        }
        else
        {
            if(!has_native_layout<Bytes>(needle) &&
               !has_native_layout<ByteArray>(needle))
                return active_thread()->set_pending_builtin_exception_string(
                    L"TypeError",
                    L"argument should be integer or bytes-like object");
            result = search_bytes<Operation>(
                source.extract()->view(), stored_byte_view(needle), start, end);
        }
        if constexpr(Operation == ByteArraySearch::Index ||
                     Operation == ByteArraySearch::RIndex)
        {
            if(result < 0)
                return active_thread()->set_pending_builtin_exception_string(
                    L"ValueError", L"subsection not found");
        }
        return CL_TRY(int_value_from_int64(thread, result));
    }

    enum class ByteArrayTail
    {
        Prefix,
        Suffix
    };

    template <ByteArrayTail Tail>
    static bool match_tail(std::span<const uint8_t> source,
                           std::span<const uint8_t> needle, size_t start,
                           size_t end)
    {
        if(start > end || needle.size() > end - start)
            return false;
        if(needle.empty())
            return true;
        size_t offset =
            Tail == ByteArrayTail::Prefix ? start : end - needle.size();
        return std::equal(needle.begin(), needle.end(),
                          source.begin() + static_cast<ptrdiff_t>(offset));
    }

    template <ByteArrayTail Tail>
    static Value native_tailmatch(ThreadState *, Value self, Value candidate,
                                  Value start_value, Value end_value)
    {
        constexpr const wchar_t *method =
            Tail == ByteArrayTail::Prefix ? L"startswith" : L"endswith";
        Owned<TValue<ByteArray>> source(CL_TRY(receiver(self, method)));
        Owned<Value> candidate_owner(candidate);
        size_t start, end;
        CL_PROPAGATE_EXCEPTION(normalize_search_range(
            static_cast<int64_t>(source.extract()->size()), start_value,
            end_value, start, end));
        // Read stored tuple elements directly, bypassing subclass hooks.
        // Validate each candidate only when reached, even when the range cannot
        // match.
        if(has_native_layout<Tuple>(candidate))
        {
            auto tuple = TValue<Tuple>::from_value_assumed(candidate);
            for(size_t i = 0; i < tuple.extract()->size(); ++i)
            {
                Value item = tuple.extract()->item_unchecked(i);
                if(!has_native_layout<Bytes>(item) &&
                   !has_native_layout<ByteArray>(item))
                    return active_thread()
                        ->set_pending_builtin_exception_string(
                            L"TypeError", L"a bytes-like object is required");
                if(match_tail<Tail>(source.extract()->view(),
                                    stored_byte_view(item), start, end))
                    return Value::True();
            }
            return Value::False();
        }
        if(!has_native_layout<Bytes>(candidate) &&
           !has_native_layout<ByteArray>(candidate))
            return active_thread()->set_pending_builtin_exception_string(
                L"TypeError",
                Tail == ByteArrayTail::Prefix
                    ? L"startswith first arg must be bytes or a tuple of bytes"
                    : L"endswith first arg must be bytes or a tuple of bytes");
        return match_tail<Tail>(source.extract()->view(),
                                stored_byte_view(candidate), start, end)
                   ? Value::True()
                   : Value::False();
    }

    static std::strong_ordering
    compare_byte_views(std::span<const uint8_t> left,
                       std::span<const uint8_t> right)
    {
        return std::lexicographical_compare_three_way(
            left.begin(), left.end(), right.begin(), right.end());
    }

    struct ByteArrayEqOperator
    {
        static constexpr const wchar_t *method = L"__eq__";
        bool operator()(std::span<const uint8_t> left,
                        std::span<const uint8_t> right) const
        {
            return std::equal(left.begin(), left.end(), right.begin(),
                              right.end());
        }
    };

    struct ByteArrayNeOperator
    {
        static constexpr const wchar_t *method = L"__ne__";
        bool operator()(std::span<const uint8_t> left,
                        std::span<const uint8_t> right) const
        {
            return !ByteArrayEqOperator{}(left, right);
        }
    };

    struct ByteArrayLtOperator
    {
        static constexpr const wchar_t *method = L"__lt__";
        bool operator()(std::span<const uint8_t> left,
                        std::span<const uint8_t> right) const
        {
            return compare_byte_views(left, right) < 0;
        }
    };

    struct ByteArrayLeOperator
    {
        static constexpr const wchar_t *method = L"__le__";
        bool operator()(std::span<const uint8_t> left,
                        std::span<const uint8_t> right) const
        {
            return compare_byte_views(left, right) <= 0;
        }
    };

    struct ByteArrayGtOperator
    {
        static constexpr const wchar_t *method = L"__gt__";
        bool operator()(std::span<const uint8_t> left,
                        std::span<const uint8_t> right) const
        {
            return compare_byte_views(left, right) > 0;
        }
    };

    struct ByteArrayGeOperator
    {
        static constexpr const wchar_t *method = L"__ge__";
        bool operator()(std::span<const uint8_t> left,
                        std::span<const uint8_t> right) const
        {
            return compare_byte_views(left, right) >= 0;
        }
    };

    template <typename ComparisonOp>
    static Value native_compare(ThreadState *, Value self, Value other)
    {
        auto array = CL_TRY(receiver(self, ComparisonOp::method));
        if(!has_native_layout<ByteArray>(other) &&
           !has_native_layout<Bytes>(other))
            return Value::NotImplemented();
        return ComparisonOp{}(array.extract()->view(), stored_byte_view(other))
                   ? Value::True()
                   : Value::False();
    }

    template <typename ComparisonOp>
    static Value trusted_compare(ThreadState *, Value left, Value right)
    {
        return ComparisonOp{}(stored_byte_view(left), stored_byte_view(right))
                   ? Value::True()
                   : Value::False();
    }

    template <typename NormalOperator, typename ReflectedOperator>
    static TrustedResolution
    resolve_compare(VirtualMachine *vm, ShapeKey left, ShapeKey right,
                    TrustedHandlerOperandOrder order, TrustedHandlerArity arity)
    {
        if(arity != TrustedHandlerArity::Binary)
            return TrustedResolution::no_trusted_handler_call_untrusted();
        auto lhs = vm->shape_for_key(left)->get_class();
        auto rhs = vm->shape_for_key(right)->get_class();
        auto receiver_class =
            order == TrustedHandlerOperandOrder::Reflected ? rhs : lhs;
        auto other_class =
            order == TrustedHandlerOperandOrder::Reflected ? lhs : rhs;
        if(receiver_class != vm->bytearray_class() ||
           (other_class != vm->bytearray_class() &&
            other_class != vm->bytes_class()))
            return TrustedResolution::no_trusted_handler_call_untrusted();
        // Handlers receive opcode operand order. Reflected ordering therefore
        // selects the opposite operation rather than swapping the operands.
        if(order == TrustedHandlerOperandOrder::Reflected)
            return TrustedResolution::call_trusted(
                trusted_compare<ReflectedOperator>);
        return TrustedResolution::call_trusted(trusted_compare<NormalOperator>);
    }

    static Value native_repr(ThreadState *, Value self)
    {
        auto array = CL_TRY(receiver(self, L"__repr__"));
        auto view = array.extract()->view();
        if(view.size() > (static_cast<size_t>(value_smi_max) - 14) / 4)
            return active_thread()->set_pending_builtin_exception_string(
                L"OverflowError",
                L"bytearray object is too large to make repr");
        bool single = std::find(view.begin(), view.end(), '\'') != view.end();
        bool double_quote =
            std::find(view.begin(), view.end(), '"') != view.end();
        wchar_t quote = single && !double_quote ? L'"' : L'\'';
        StringBuilder builder;
        builder.append_c_str(L"bytearray(b");
        builder.append_char(quote);
        static constexpr wchar_t digits[] = L"0123456789abcdef";
        for(uint8_t byte: view)
        {
            if(byte == '\\' || byte == '\'')
            {
                builder.append_char(L'\\');
                builder.append_char(byte);
            }
            else if(byte == '\t')
                builder.append_c_str(L"\\t");
            else if(byte == '\n')
                builder.append_c_str(L"\\n");
            else if(byte == '\r')
                builder.append_c_str(L"\\r");
            else if(byte >= 32 && byte <= 126)
                builder.append_char(byte);
            else
            {
                builder.append_c_str(L"\\x");
                builder.append_char(digits[byte >> 4]);
                builder.append_char(digits[byte & 15]);
            }
        }
        builder.append_char(quote);
        builder.append_char(L')');
        return builder.finish();
    }

    static Value native_iter(ThreadState *thread, Value self)
    {
        return thread
            ->make_object_value<ByteArrayIterator>(
                CL_TRY(receiver(self, L"__iter__")))
            .raw_value();
    }
    static Value iterator_iter(ThreadState *, Value self)
    {
        (void)CL_TRY(TValue<ByteArrayIterator>::from_value_or_raise(
            self, L"TypeError", L"expected bytearray_iterator receiver"));
        return self;
    }
    static Value iterator_next(ThreadState *thread, Value self)
    {
        auto iterator = CL_TRY(TValue<ByteArrayIterator>::from_value_or_raise(
            self, L"TypeError", L"expected bytearray_iterator receiver"));
        if(!iterator.extract()->source.value())
            return thread->set_pending_stop_iteration_no_value();
        TValue<ByteArray> source = iterator.extract()->source.value().value();
        size_t pos = static_cast<size_t>(iterator.extract()->index.extract());
        if(pos >= source.extract()->size())
        {
            iterator.extract()->source = Optional<TValue<ByteArray>>::none();
            return thread->set_pending_stop_iteration_no_value();
        }
        iterator.extract()->index =
            TValue<SMI>::from_smi(static_cast<int64_t>(pos + 1));
        return Value::from_smi(source.extract()->items[pos]);
    }

    BuiltinClassDefinition make_bytearray_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId layouts[] = {NativeLayoutId::ByteArray};
        auto cls = ClassObject::make_builtin_class<ByteArray>(
            vm->get_or_create_interned_string_value(L"bytearray"), 0, nullptr,
            0, vm->object_class(), SubclassPolicy::Disallowed);
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Public);
    }
    BuiltinClassDefinition make_bytearray_iterator_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId layouts[] = {
            NativeLayoutId::ByteArrayIterator};
        BuiltinClassMethod methods[] = {
            {vm->get_or_create_interned_string_value(L"__iter__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, iterator_iter),
                 "bytearray iterator")
                 .raw_value()},
            {vm->get_or_create_interned_string_value(L"__next__"),
             unwrap_bootstrap_expected(
                 vm, make_intrinsic_function(vm, iterator_next),
                 "bytearray iterator")
                 .raw_value()}};
        auto cls = ClassObject::make_builtin_class<ByteArrayIterator>(
            vm->get_or_create_interned_string_value(L"bytearray_iterator"), 0,
            methods, std::size(methods), vm->object_class(),
            SubclassPolicy::Disallowed);
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Internal);
    }
    void install_bytearray_class_methods(VirtualMachine *vm)
    {
        Owned<TValue<Tuple>> pop_defaults(
            vm->get_default_thread()->make_object_value<Tuple>(1));
        pop_defaults.extract()->initialize_item_unchecked(0,
                                                          Value::from_smi(-1));
        Owned<TValue<Tuple>> search_defaults(
            vm->get_default_thread()->make_object_value<Tuple>(2));
        search_defaults.extract()->initialize_item_unchecked(0, Value::None());
        search_defaults.extract()->initialize_item_unchecked(1, Value::None());
        Owned<TValue<Tuple>> split_defaults(
            vm->get_default_thread()->make_object_value<Tuple>(2));
        split_defaults.extract()->initialize_item_unchecked(0, Value::None());
        split_defaults.extract()->initialize_item_unchecked(
            1, Value::from_smi(-1));
        static constexpr const wchar_t *split_names[] = {L"sep", L"maxsplit"};
        vm->register_trusted_handler(trusted_compare<ByteArrayEqOperator>,
                                     TrustedHandlerEffects::None);
        vm->register_trusted_handler(trusted_compare<ByteArrayNeOperator>,
                                     TrustedHandlerEffects::None);
        vm->register_trusted_handler(trusted_compare<ByteArrayLtOperator>,
                                     TrustedHandlerEffects::None);
        vm->register_trusted_handler(trusted_compare<ByteArrayLeOperator>,
                                     TrustedHandlerEffects::None);
        vm->register_trusted_handler(trusted_compare<ByteArrayGtOperator>,
                                     TrustedHandlerEffects::None);
        vm->register_trusted_handler(trusted_compare<ByteArrayGeOperator>,
                                     TrustedHandlerEffects::None);
        vm->register_trusted_handler(trusted_getitem<ItemKeyKind::Smi>,
                                     TrustedHandlerEffects::Raise);
        vm->register_trusted_handler(trusted_getitem<ItemKeyKind::Slice>,
                                     TrustedHandlerEffects::Raise |
                                         TrustedHandlerEffects::Allocate);
        vm->register_trusted_handler(trusted_setitem<ItemKeyKind::Smi>,
                                     TrustedHandlerEffects::Raise);
        // The RHS is not guarded by the IC. Every replay may enter Python.
        vm->register_trusted_handler(trusted_setitem<ItemKeyKind::Slice>,
                                     TrustedHandlerEffects::Raise |
                                         TrustedHandlerEffects::Allocate |
                                         TrustedHandlerEffects::Safepoint |
                                         TrustedHandlerEffects::CallPython);
        vm->register_trusted_handler(trusted_delitem<ItemKeyKind::Smi>,
                                     TrustedHandlerEffects::Raise);
        vm->register_trusted_handler(trusted_delitem<ItemKeyKind::Slice>,
                                     TrustedHandlerEffects::Raise);
        BuiltinIntrinsicMethod methods[] = {
            with_keyword_parameter_names(
                with_defaults(
                    builtin_intrinsic_method(
                        L"split", native_split<SearchDirection::Forward>),
                    split_defaults.value()),
                split_names, 2, 1),
            with_keyword_parameter_names(
                with_defaults(
                    builtin_intrinsic_method(
                        L"rsplit", native_split<SearchDirection::Reverse>),
                    split_defaults.value()),
                split_names, 2, 1),
            builtin_intrinsic_method(
                L"partition", native_partition<SearchDirection::Forward>),
            builtin_intrinsic_method(
                L"rpartition", native_partition<SearchDirection::Reverse>),
            builtin_intrinsic_method(L"__len__", native_len),
            builtin_intrinsic_method(L"__repr__", native_repr),
            builtin_intrinsic_method(L"__str__", native_repr),
            builtin_intrinsic_method(L"__iter__", native_iter),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__getitem__", native_getitem),
                resolve_item<trusted_getitem<ItemKeyKind::Smi>,
                             trusted_getitem<ItemKeyKind::Slice>,
                             TrustedHandlerArity::Binary>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__setitem__", native_setitem),
                resolve_item<trusted_setitem<ItemKeyKind::Smi>,
                             trusted_setitem<ItemKeyKind::Slice>,
                             TrustedHandlerArity::Ternary>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__delitem__", native_delitem),
                resolve_item<trusted_delitem<ItemKeyKind::Smi>,
                             trusted_delitem<ItemKeyKind::Slice>,
                             TrustedHandlerArity::Binary>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__eq__",
                                         native_compare<ByteArrayEqOperator>),
                resolve_compare<ByteArrayEqOperator, ByteArrayEqOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__ne__",
                                         native_compare<ByteArrayNeOperator>),
                resolve_compare<ByteArrayNeOperator, ByteArrayNeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__lt__",
                                         native_compare<ByteArrayLtOperator>),
                resolve_compare<ByteArrayLtOperator, ByteArrayGtOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__le__",
                                         native_compare<ByteArrayLeOperator>),
                resolve_compare<ByteArrayLeOperator, ByteArrayGeOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__gt__",
                                         native_compare<ByteArrayGtOperator>),
                resolve_compare<ByteArrayGtOperator, ByteArrayLtOperator>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__ge__",
                                         native_compare<ByteArrayGeOperator>),
                resolve_compare<ByteArrayGeOperator, ByteArrayLeOperator>),
            builtin_intrinsic_method(L"append", native_append),
            builtin_intrinsic_method(L"clear", native_clear),
            with_defaults(builtin_intrinsic_method(L"pop", native_pop),
                          pop_defaults.value()),
            builtin_intrinsic_method(L"insert", native_insert),
            builtin_intrinsic_method(L"remove", native_remove),
            builtin_intrinsic_method(L"reverse", native_reverse),
            builtin_intrinsic_method(L"copy", native_copy),
            with_defaults(builtin_intrinsic_method(
                              L"find", native_search<ByteArraySearch::Find>),
                          search_defaults.value()),
            with_defaults(builtin_intrinsic_method(
                              L"rfind", native_search<ByteArraySearch::RFind>),
                          search_defaults.value()),
            with_defaults(builtin_intrinsic_method(
                              L"index", native_search<ByteArraySearch::Index>),
                          search_defaults.value()),
            with_defaults(
                builtin_intrinsic_method(
                    L"rindex", native_search<ByteArraySearch::RIndex>),
                search_defaults.value()),
            with_defaults(builtin_intrinsic_method(
                              L"count", native_search<ByteArraySearch::Count>),
                          search_defaults.value()),
            with_defaults(
                builtin_intrinsic_method(
                    L"startswith", native_tailmatch<ByteArrayTail::Prefix>),
                search_defaults.value()),
            with_defaults(
                builtin_intrinsic_method(
                    L"endswith", native_tailmatch<ByteArrayTail::Suffix>),
                search_defaults.value())};
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->bytearray_class(),
                                              methods, std::size(methods)),
            "installing bytearray methods");
        bool stored = vm->bytearray_class()->define_own_property(
            vm->get_or_create_interned_string_value(L"__hash__"), Value::None(),
            descriptor_flag(DescriptorFlag::ReadOnly) |
                descriptor_flag(DescriptorFlag::StableSlot));
        assert(stored);
        (void)stored;
    }
    void install_bytearray_helpers(VirtualMachine *vm, ModuleObject *module)
    {
        BuiltinIntrinsicMethod helpers[] = {
            builtin_intrinsic_method(L"_clover_bytearray_allocate", allocate),
            builtin_intrinsic_method(L"_clover_bytearray_initialize_native",
                                     initialize_native),
            builtin_intrinsic_method(L"_clover_bytearray_copy_native",
                                     copy_native),
            builtin_intrinsic_method(L"_clover_bytearray_extend_native",
                                     extend_native),
            builtin_intrinsic_method(L"_clover_bytearray_append",
                                     native_append)};
        for(const auto &helper: helpers)
        {
            auto function = unwrap_bootstrap_expected(
                vm, make_intrinsic_function(vm, helper),
                "creating bytearray helper");
            unwrap_bootstrap_expected(
                vm,
                module->set_own_property(
                    vm->get_or_create_interned_string_value(helper.name),
                    function.raw_value()),
                "installing bytearray helper");
        }
    }
    void install_bytearray_python_methods(VirtualMachine *vm,
                                          ModuleObject *module)
    {
        const wchar_t *names[] = {L"__new__", L"extend"};
        const wchar_t *helpers[] = {L"_clover_bytearray_new",
                                    L"_clover_bytearray_extend"};
        for(size_t n = 0; n < std::size(names); ++n)
        {
            Value function = module->get_own_property(
                vm->get_or_create_interned_string_value(helpers[n]));
            if(!has_native_layout<Function>(function))
                fatal("missing bytearray Python method");
            bool stored = vm->bytearray_class()->define_own_property(
                vm->get_or_create_interned_string_value(names[n]), function,
                descriptor_flag(DescriptorFlag::ReadOnly) |
                    descriptor_flag(DescriptorFlag::StableSlot));
            assert(stored);
            (void)stored;
        }
    }
}  // namespace cl
