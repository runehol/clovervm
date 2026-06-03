#ifndef CL_MODULE_LOADER_OBJECT_H
#define CL_MODULE_LOADER_OBJECT_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class ModuleLoaderObject : public SlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ModuleLoaderObject;
        static constexpr uint32_t kKindSlot = 0;
        static constexpr uint32_t kNameSlot = 1;
        static constexpr uint32_t kPathSlot = 2;
        static constexpr uint32_t kInlineSlotCount = 3;

        ModuleLoaderObject(ClassObject *cls, Value kind, Value name,
                           Value path);

        Member<Value> kind;
        Member<Value> name;
        Member<Value> path;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ModuleLoaderObject, SlotObject, 3);
        CL_DECLARE_STATIC_OBJECT_SIZE(ModuleLoaderObject);
    };

    static_assert(CL_OFFSETOF(ModuleLoaderObject, kind) ==
                  sizeof(SlotObject) +
                      ModuleLoaderObject::kKindSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(ModuleLoaderObject, name) ==
                  sizeof(SlotObject) +
                      ModuleLoaderObject::kNameSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(ModuleLoaderObject, path) ==
                  sizeof(SlotObject) +
                      ModuleLoaderObject::kPathSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<ModuleLoaderObject>);

    BuiltinClassDefinition make_module_loader_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_MODULE_LOADER_OBJECT_H
