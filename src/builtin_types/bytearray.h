#ifndef CL_BYTEARRAY_H
#define CL_BYTEARRAY_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/vm_array.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

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

    static_assert(std::is_trivially_destructible_v<ByteArray>);

    BuiltinClassDefinition make_bytearray_class(VirtualMachine *vm);
    void install_bytearray_class_methods(VirtualMachine *vm);
    void install_bytearray_helpers(VirtualMachine *vm, ModuleObject *module);
    void install_bytearray_python_methods(VirtualMachine *vm,
                                          ModuleObject *module);
}  // namespace cl
#endif  // CL_BYTEARRAY_H
