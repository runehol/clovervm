#include "builtin_types/random_engine.h"

#include "builtin_types/bytes.h"
#include "builtin_types/float.h"
#include "builtin_types/int.h"
#include "builtin_types/module_object.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/owned.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>

/*
 * MT19937 twist, temper and array initialization adapted from mt19937ar
 * (2002/01/26), by Takuji Nishimura and Makoto Matsumoto.
 * https://www.math.sci.hiroshima-u.ac.jp/m-mat/MT/MT2002/emt19937ar.html
 *
 * See dependencies/mt19937-LICENSE for redistribution terms.
 */

namespace cl
{
    void MT19937::seed(std::span<const uint32_t> key)
    {
        assert(!key.empty());
        words[0] = 19650218U;
        for(uint32_t i = 1; i < word_count; ++i)
        {
            uint32_t previous = words[i - 1];
            words[i] = 1812433253U * (previous ^ (previous >> 30)) + i;
        }

        uint32_t i = 1;
        size_t key_index = 0;
        const auto advance = [&]() {
            if(++i == word_count)
            {
                words[0] = words.back();
                i = 1;
            }
        };
        for(size_t remaining = std::max(word_count, key.size()); remaining > 0;
            --remaining)
        {
            uint32_t previous = words[i - 1];
            words[i] = (words[i] ^ ((previous ^ (previous >> 30)) * 1664525U)) +
                       key[key_index] + static_cast<uint32_t>(key_index);
            advance();
            if(++key_index == key.size())
                key_index = 0;
        }
        for(size_t remaining = word_count - 1; remaining > 0; --remaining)
        {
            uint32_t previous = words[i - 1];
            words[i] =
                (words[i] ^ ((previous ^ (previous >> 30)) * 1566083941U)) - i;
            advance();
        }
        words[0] = 0x80000000U;
        index = word_count;
    }

    uint32_t MT19937::next_word()
    {
        if(index == word_count)
        {
            constexpr size_t offset = 397;
            const auto twist = [&](size_t dest, size_t next, size_t source) {
                uint32_t joined =
                    (words[dest] & 0x80000000U) | (words[next] & 0x7fffffffU);
                words[dest] = words[source] ^ (joined >> 1) ^
                              ((joined & 1U) * 0x9908b0dfU);
            };
            for(size_t i = 0; i < word_count - offset; ++i)
                twist(i, i + 1, i + offset);
            // After wrapping, the recurrence uses the newly updated words.
            for(size_t i = word_count - offset; i < word_count - 1; ++i)
                twist(i, i + 1, i - (word_count - offset));
            twist(word_count - 1, 0, offset - 1);
            index = 0;
        }

        uint32_t word = words[index++];
        word ^= word >> 11;
        word ^= (word << 7) & 0x9d2c5680U;
        word ^= (word << 15) & 0xefc60000U;
        return word ^ (word >> 18);
    }

    uint32_t MT19937::next_bits(unsigned width)
    {
        assert(width > 0 && width <= 32);
        return next_word() >> (32 - width);
    }

    namespace
    {
        [[nodiscard]] Expected<TValue<RandomEngine>> get_engine(Value engine)
        {
            return TValue<RandomEngine>::from_value_or_raise(
                engine, L"TypeError", L"expected a native random engine");
        }

        [[nodiscard]] Expected<ConstBigIntView>
        integer_view(Value value, SmiViewStorage *scratch)
        {
            if(!is_intlike_value(value))
                return Expected<ConstBigIntView>::raise_exception(
                    L"TypeError", L"expected an integer");
            return Expected<ConstBigIntView>::ok(
                intlike_value_bigint_view(value, scratch));
        }

        [[nodiscard]] Value seed_from_int(ThreadState *thread, Value seed)
        {
            SmiViewStorage scratch;
            auto magnitude = CL_TRY(integer_view(seed, &scratch));
            constexpr uint32_t zero = 0;
            MT19937 state;
            state.seed(magnitude.n_digits == 0
                           ? std::span<const uint32_t>{&zero, 1}
                           : std::span<const uint32_t>{magnitude.digits,
                                                       magnitude.n_digits});
            return thread->make_object_value<RandomEngine>(state).raw_value();
        }

        [[nodiscard]] Value seed_from_entropy(ThreadState *thread,
                                              Value entropy)
        {
            auto bytes = CL_TRY(TValue<Bytes>::from_value_or_raise(
                entropy, L"TypeError", L"expected entropy bytes"));
            std::array<uint32_t, MT19937::word_count> key;
            {
                auto data = bytes_view(bytes);
                if(data.size() != key.size() * sizeof(uint32_t))
                    return thread->set_pending_builtin_exception_string(
                        L"ValueError", L"wrong size for random seed bytes");
                for(size_t i = 0; i < key.size(); ++i)
                {
                    const auto *word = data.data() + i * sizeof(uint32_t);
                    key[i] = uint32_t{word[0]} | (uint32_t{word[1]} << 8) |
                             (uint32_t{word[2]} << 16) |
                             (uint32_t{word[3]} << 24);
                }
            }
            MT19937 state;
            state.seed(key);
            return thread->make_object_value<RandomEngine>(state).raw_value();
        }

        [[nodiscard]] Value random_float(ThreadState *thread, Value engine)
        {
            auto &state = CL_TRY(get_engine(engine)).extract()->state;
            uint64_t a = state.next_bits(27);
            uint64_t b = state.next_bits(26);
            return box_float(thread,
                             static_cast<double>((a << 26) | b) * 0x1.0p-53);
        }

        [[nodiscard]] Value getrandbits(ThreadState *thread, Value engine,
                                        Value width)
        {
            auto state = CL_TRY(get_engine(engine));
            SmiViewStorage width_storage;
            auto view = CL_TRY(integer_view(width, &width_storage));
            if(view.signum < 0)
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"number of bits must be non-negative");
            uint64_t bits = CL_TRY(bigint_to_uint64(view));
            if(bits == 0)
                return Value::from_smi(0);
            if(bits <= 64)
            {
                auto &generator = state.extract()->state;
                uint64_t result = generator.next_bits(
                    static_cast<unsigned>(std::min<uint64_t>(bits, 32)));
                if(bits > 32)
                    result |= uint64_t{generator.next_bits(
                                  static_cast<unsigned>(bits - 32))}
                              << 32;
                return CL_TRY(int_value_from_uint64(thread, result));
            }

            uint64_t count = (bits - 1) / kDigitBits + 1;
            if(count > (std::numeric_limits<size_t>::max() - sizeof(BigInt)) /
                           sizeof(digit_t))
                return thread->set_pending_builtin_exception_string(
                    L"OverflowError", L"random integer is too large");
            size_t size = static_cast<size_t>(count);
            std::unique_ptr<digit_t, decltype(&std::free)> digits(
                static_cast<digit_t *>(std::malloc(size * sizeof(digit_t))),
                &std::free);
            if(!digits)
                return thread->set_pending_builtin_exception_none(
                    L"MemoryError");

            auto &generator = state.extract()->state;
            size_t last = size - 1;
            for(size_t i = 0; i < last; ++i)
                digits.get()[i] = generator.next_word();
            digits.get()[last] = generator.next_bits(
                static_cast<unsigned>((bits - 1) % kDigitBits + 1));
            return CL_TRY(finalize_bigint(
                thread, normalize_bigint_view({size, 1, digits.get()})));
        }

        [[nodiscard]] Value getstate(ThreadState *thread, Value engine)
        {
            Owned<TValue<RandomEngine>> owner(CL_TRY(get_engine(engine)));
            auto result =
                thread->make_object_value<Tuple>(MT19937::word_count + 1);
            const auto &state = owner.extract()->state;
            for(size_t i = 0; i < MT19937::word_count; ++i)
                result.extract()->initialize_item_unchecked(
                    i, Value::from_smi(state.words[i]));
            result.extract()->initialize_item_unchecked(
                MT19937::word_count, Value::from_smi(state.index));
            return result.raw_value();
        }

        [[nodiscard]] Value restore_state(ThreadState *thread, Value snapshot,
                                          Value legacy)
        {
            auto tuple = CL_TRY(TValue<Tuple>::from_value_or_raise(
                snapshot, L"TypeError",
                L"random state vector must be a tuple"));
            constexpr size_t count = MT19937::word_count;
            if(tuple.extract()->size() != count + 1)
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"random state vector is the wrong size");
            if(!legacy.is_bool())
                return thread->set_pending_builtin_exception_string(
                    L"TypeError", L"expected a boolean legacy-state flag");

            MT19937 state;
            SmiViewStorage scratch;
            for(size_t i = 0; i < count; ++i)
            {
                auto view = CL_TRY(
                    integer_view(tuple.extract()->item_unchecked(i), &scratch));
                if(legacy == Value::False() &&
                   (view.signum < 0 || view.n_digits > 1))
                    return thread->set_pending_builtin_exception_string(
                        L"OverflowError",
                        L"random state word is outside uint32");
                uint32_t low = view.n_digits == 0 ? 0 : view.digits[0];
                state.words[i] = view.signum < 0 ? 0U - low : low;
            }
            auto index = CL_TRY(
                integer_view(tuple.extract()->item_unchecked(count), &scratch));
            if(index.signum < 0 || index.n_digits > 1 ||
               (index.n_digits == 1 && index.digits[0] > count))
                return thread->set_pending_builtin_exception_string(
                    L"ValueError", L"invalid random state index");
            state.index = index.n_digits == 0 ? 0 : index.digits[0];
            return thread->make_object_value<RandomEngine>(state).raw_value();
        }
    }  // namespace

    BuiltinClassDefinition make_random_engine_class(VirtualMachine *vm)
    {
        ClassObject *cls = ClassObject::make_builtin_class<RandomEngine>(
            vm->get_or_create_interned_string_value(L"_RandomEngine"), 0,
            nullptr, 0, vm->object_class(), SubclassPolicy::Disallowed,
            immutable_shape_flags());
        static constexpr NativeLayoutId layouts[] = {
            NativeLayoutId::RandomEngine};
        return builtin_class_definition(cls, layouts,
                                        BuiltinsVisibility::Internal);
    }

    [[nodiscard]] Value install_random_engine_functions(ThreadState *thread,
                                                        ModuleObject *module)
    {
        auto *vm = thread->get_machine();
        const BuiltinIntrinsicMethod functions[] = {
            builtin_intrinsic_method(L"seed_from_int", seed_from_int),
            builtin_intrinsic_method(L"seed_from_entropy", seed_from_entropy),
            builtin_intrinsic_method(L"random", random_float),
            builtin_intrinsic_method(L"getrandbits", getrandbits),
            builtin_intrinsic_method(L"getstate", getstate),
            builtin_intrinsic_method(L"restore_state", restore_state),
        };
        for(const auto &function: functions)
        {
            Owned<TValue<Function>> native(
                CL_TRY(make_intrinsic_function(vm, function)));
            CL_TRY(module->set_own_property(
                vm->get_or_create_interned_string_value(function.name),
                native.raw_value()));
        }
        return Value::None();
    }
}  // namespace cl
