#ifndef CL_BYTEARRAY_H
#define CL_BYTEARRAY_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/vm_array.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace cl
{
    class ModuleObject;
    class VirtualMachine;

    class ByteArray : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ByteArray;
        explicit ByteArray(ClassObject *cls) : Object(cls, native_layout) {}

        size_t size() const { return items.size(); }
        std::span<const uint8_t> view() const { return {items.data(), size()}; }

        // Native object operations only. No pointers into items may survive
        // a resize, Python callback, or VM safepoint.
        RawArray<uint8_t> items;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(
            ByteArray, Object, RawArray<uint8_t>::embedded_value_count);
        CL_DECLARE_STATIC_OBJECT_SIZE(ByteArray);
    };

    class ByteArrayIterator : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ByteArrayIterator;
        ByteArrayIterator(ClassObject *cls, TValue<ByteArray> source)
            : Object(cls, native_layout),
              source(Optional<TValue<ByteArray>>::some(source)),
              index(TValue<SMI>::from_smi(0))
        {
        }
        Member<Optional<TValue<ByteArray>>> source;
        Member<TValue<SMI>> index;
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ByteArrayIterator, Object, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(ByteArrayIterator);
    };

    static_assert(std::is_trivially_destructible_v<ByteArray>);
    static_assert(std::is_trivially_destructible_v<ByteArrayIterator>);

    // Narrow synchronous native borrowing contract, NOT a buffer export or a
    // pin. The callback must not allocate, enter Python, poll a safepoint,
    // reclaim, resize/mutate through another alias, or retain the span/pointer.
    // Native execution must remain exclusive for this entire scope. Ownership
    // preserves lifetime only; any future moving/concurrent collector must
    // preserve the no-collection scope before this API can continue to be used.
    // Results/errno may be saved in scalar locals; translate them after return.
    template <typename Fn>
    void with_bytearray_writable_bytes(TValue<ByteArray> value, Fn &&operation)
    {
        static_assert(
            std::is_nothrow_invocable_r_v<void, Fn, std::span<uint8_t>>);
        static_assert(
            std::is_same_v<std::invoke_result_t<Fn, std::span<uint8_t>>, void>);
        Owned<TValue<ByteArray>> owner(value);
        std::forward<Fn>(operation)(std::span<uint8_t>(
            owner.extract()->items.data(), owner.extract()->size()));
    }

    BuiltinClassDefinition make_bytearray_class(VirtualMachine *vm);
    BuiltinClassDefinition make_bytearray_iterator_class(VirtualMachine *vm);
    void install_bytearray_class_methods(VirtualMachine *vm);
    void install_bytearray_helpers(VirtualMachine *vm, ModuleObject *module);
    void install_bytearray_python_methods(VirtualMachine *vm,
                                          ModuleObject *module);
}  // namespace cl
#endif  // CL_BYTEARRAY_H
