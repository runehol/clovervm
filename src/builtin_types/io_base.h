#ifndef CL_IO_BASE_H
#define CL_IO_BASE_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include <type_traits>

namespace cl
{
    class ModuleObject;
    class ThreadState;
    class VirtualMachine;

    class IOBaseObject : public InlineSlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::IOBase;
        static constexpr uint32_t kPredefinedInlineSlotCount = 1;
        static constexpr uint32_t kInlineSlotCount = 1;

        explicit IOBaseObject(ClassObject *cls,
                              NativeLayoutId layout = native_layout)
            : InlineSlotObject(cls, layout),
              base_closed(TValue<Bool>::from_bool(false))
        {
        }

        Member<TValue<Bool>> base_closed;
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(IOBaseObject, InlineSlotObject, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(IOBaseObject);
    };

    class RawIOBaseObject : public IOBaseObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::RawIOBase;
        explicit RawIOBaseObject(ClassObject *cls,
                                 NativeLayoutId layout = native_layout)
            : IOBaseObject(cls, layout)
        {
        }
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(RawIOBaseObject, IOBaseObject, 0);
        CL_DECLARE_STATIC_OBJECT_SIZE(RawIOBaseObject);
    };
    static_assert(CL_OFFSETOF(IOBaseObject, base_closed) ==
                  sizeof(InlineSlotObject));
    static_assert(sizeof(RawIOBaseObject) == sizeof(IOBaseObject));
    static_assert(std::is_trivially_destructible_v<IOBaseObject>);
    static_assert(std::is_trivially_destructible_v<RawIOBaseObject>);

    constexpr bool is_io_base_layout(NativeLayoutId layout)
    {
        return layout == NativeLayoutId::IOBase ||
               layout == NativeLayoutId::RawIOBase ||
               layout == NativeLayoutId::FileIO;
    }

    [[nodiscard]] Value allocate_io_base(ThreadState *thread, Value cls);
    [[nodiscard]] Value close_io_base(ThreadState *thread, Value self);
    BuiltinClassDefinition make_io_base_class(VirtualMachine *vm);
    BuiltinClassDefinition make_raw_io_base_class(VirtualMachine *vm);
    void install_io_base_methods(VirtualMachine *vm);
    void install_io_base_helpers(VirtualMachine *vm, ModuleObject *module);
    void install_io_base_python_methods(VirtualMachine *vm,
                                        ModuleObject *module);
}  // namespace cl

#endif  // CL_IO_BASE_H
