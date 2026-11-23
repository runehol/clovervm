#ifndef CL_BOUND_METHOD_H
#define CL_BOUND_METHOD_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"

namespace cl
{
    class ModuleObject;
    class VirtualMachine;

    class BoundMethod : public InlineSlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::BoundMethod;
        static constexpr uint32_t kCallableSlot = 0;
        static constexpr uint32_t kReceiverSlot = 1;
        static constexpr uint32_t kPredefinedInlineSlotCount =
            kReceiverSlot + 1;
        static constexpr uint32_t kInlineSlotCount = kPredefinedInlineSlotCount;

        BoundMethod(ClassObject *cls, Value callable, Value receiver)
            : InlineSlotObject(cls, native_layout), callable_(callable),
              receiver_(receiver)
        {
        }

        Value callable() const { return callable_.raw_value(); }
        Value receiver() const { return receiver_.raw_value(); }

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(BoundMethod, InlineSlotObject, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(BoundMethod);

    private:
        Member<Value> callable_;
        Member<Value> receiver_;
    };

    BuiltinClassDefinition make_bound_method_class(VirtualMachine *vm);
    void
    install_bound_method_class_methods(VirtualMachine *vm,
                                       ModuleObject *implementation_module);
}  // namespace cl

#endif  // CL_BOUND_METHOD_H
