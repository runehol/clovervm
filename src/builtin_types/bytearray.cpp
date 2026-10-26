#include "builtin_types/bytearray.h"
#include "builtin_types/bytes.h"
#include "builtin_types/int.h"
#include "builtin_types/module_object.h"
#include "builtin_types/string_builder.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <algorithm>
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

    template <bool NotEqual>
    static Value native_compare(ThreadState *, Value self, Value other)
    {
        auto array = CL_TRY(receiver(self, NotEqual ? L"__ne__" : L"__eq__"));
        std::span<const uint8_t> rhs;
        if(has_native_layout<ByteArray>(other))
            rhs = other.get_ptr<ByteArray>()->view();
        else if(has_native_layout<Bytes>(other))
            rhs = bytes_view(TValue<Bytes>::from_value_assumed(other));
        else
            return Value::NotImplemented();
        bool equal =
            std::equal(array.extract()->view().begin(),
                       array.extract()->view().end(), rhs.begin(), rhs.end());
        return equal != NotEqual ? Value::True() : Value::False();
    }

    template <bool NotEqual>
    static Value trusted_compare(ThreadState *, Value left, Value right)
    {
        auto view = [](Value value) -> std::span<const uint8_t> {
            return has_native_layout<ByteArray>(value)
                       ? value.get_ptr<ByteArray>()->view()
                       : bytes_view(TValue<Bytes>::from_value_assumed(value));
        };
        auto lhs = view(left);
        auto rhs = view(right);
        bool equal = std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
        return equal != NotEqual ? Value::True() : Value::False();
    }

    template <bool NotEqual>
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
        if(receiver_class == vm->bytearray_class() &&
           (other_class == vm->bytearray_class() ||
            other_class == vm->bytes_class()))
            return TrustedResolution::call_trusted(trusted_compare<NotEqual>);
        return TrustedResolution::no_trusted_handler_call_untrusted();
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
        vm->register_trusted_handler(trusted_compare<false>,
                                     TrustedHandlerEffects::None);
        vm->register_trusted_handler(trusted_compare<true>,
                                     TrustedHandlerEffects::None);
        BuiltinIntrinsicMethod methods[] = {
            builtin_intrinsic_method(L"__len__", native_len),
            builtin_intrinsic_method(L"__repr__", native_repr),
            builtin_intrinsic_method(L"__str__", native_repr),
            builtin_intrinsic_method(L"__iter__", native_iter),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__eq__", native_compare<false>),
                resolve_compare<false>),
            with_trusted_handler_resolver(
                builtin_intrinsic_method(L"__ne__", native_compare<true>),
                resolve_compare<true>),
            builtin_intrinsic_method(L"append", native_append),
            builtin_intrinsic_method(L"clear", native_clear)};
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
        const wchar_t *names[] = {L"__new__"};
        const wchar_t *helpers[] = {L"_clover_bytearray_new"};
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
