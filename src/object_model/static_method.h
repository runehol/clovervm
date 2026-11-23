#ifndef CL_STATIC_METHOD_H
#define CL_STATIC_METHOD_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"

namespace cl
{
    class StaticMethod : public InlineSlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::StaticMethod;
        static constexpr uint32_t kWrappedSlot = 0;
        static constexpr uint32_t kPredefinedInlineSlotCount = kWrappedSlot + 1;
        static constexpr uint32_t kInlineSlotCount = kPredefinedInlineSlotCount;

        StaticMethod(ClassObject *cls, Value wrapped)
            : InlineSlotObject(cls, native_layout), wrapped_(wrapped)
        {
        }

        Value wrapped() const { return wrapped_.raw_value(); }

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(StaticMethod, InlineSlotObject, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(StaticMethod);

    private:
        Member<Value> wrapped_;
    };

    BuiltinClassDefinition make_static_method_class(VirtualMachine *vm);
    void install_static_method_class_methods(VirtualMachine *vm);
}  // namespace cl

#endif  // CL_STATIC_METHOD_H
