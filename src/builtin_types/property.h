#ifndef CL_PROPERTY_H
#define CL_PROPERTY_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"

namespace cl
{
    class ModuleObject;
    class ThreadState;
    class VirtualMachine;

    class Property : public InlineSlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::Property;
        static constexpr uint32_t kGetSlot = 0;
        static constexpr uint32_t kSetSlot = 1;
        static constexpr uint32_t kDeleteSlot = 2;
        static constexpr uint32_t kDocSlot = 3;
        static constexpr uint32_t kDocFromGetterSlot = 4;
        static constexpr uint32_t kPredefinedInlineSlotCount =
            kDocFromGetterSlot + 1;
        static constexpr uint32_t kInlineSlotCount = kPredefinedInlineSlotCount;

        explicit Property(ClassObject *cls)
            : InlineSlotObject(cls, native_layout)
        {
        }

        Member<Value> fget{Value::None()};
        Member<Value> fset{Value::None()};
        Member<Value> fdel{Value::None()};
        Member<Value> doc{Value::None()};
        Member<Value> doc_from_getter{Value::False()};

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(Property, InlineSlotObject, 5);
        CL_DECLARE_STATIC_OBJECT_SIZE(Property);
    };

    static_assert(CL_OFFSETOF(Property, fget) ==
                  sizeof(InlineSlotObject) +
                      Property::kGetSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(Property, fset) ==
                  sizeof(InlineSlotObject) +
                      Property::kSetSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(Property, fdel) ==
                  sizeof(InlineSlotObject) +
                      Property::kDeleteSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(Property, doc) ==
                  sizeof(InlineSlotObject) +
                      Property::kDocSlot * sizeof(Value));

    BuiltinClassDefinition make_property_class(VirtualMachine *vm);
    [[nodiscard]] Value allocate_property(ThreadState *thread, Value cls);
    void install_property_helpers(VirtualMachine *vm, ModuleObject *module);
    void install_property_python_methods(VirtualMachine *vm,
                                         ModuleObject *module);
}  // namespace cl

#endif  // CL_PROPERTY_H
