#ifndef CL_MODULE_SPEC_OBJECT_H
#define CL_MODULE_SPEC_OBJECT_H

#include "builtin_class_registry.h"
#include "object.h"
#include "owned.h"
#include "typed_value.h"

namespace cl
{
    class ClassObject;
    class VirtualMachine;

    class ModuleSpecObject : public SlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::ModuleSpecObject;
        static constexpr uint32_t kNameSlot = 0;
        static constexpr uint32_t kLoaderSlot = 1;
        static constexpr uint32_t kOriginSlot = 2;
        static constexpr uint32_t kSubmoduleSearchLocationsSlot = 3;
        static constexpr uint32_t kHasLocationSlot = 4;
        static constexpr uint32_t kParentSlot = 5;
        static constexpr uint32_t kInlineSlotCount = 6;

        ModuleSpecObject(ClassObject *cls, Value name, Value loader,
                         Value origin, Value submodule_search_locations,
                         Value has_location, Value parent);

        Member<Value> name;
        Member<Value> loader;
        Member<Value> origin;
        Member<Value> submodule_search_locations;
        Member<Value> has_location;
        Member<Value> parent;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(ModuleSpecObject, SlotObject, 6);
        CL_DECLARE_STATIC_OBJECT_SIZE(ModuleSpecObject);
    };

    static_assert(CL_OFFSETOF(ModuleSpecObject, name) ==
                  sizeof(SlotObject) +
                      ModuleSpecObject::kNameSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(ModuleSpecObject, loader) ==
                  sizeof(SlotObject) +
                      ModuleSpecObject::kLoaderSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(ModuleSpecObject, origin) ==
                  sizeof(SlotObject) +
                      ModuleSpecObject::kOriginSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(ModuleSpecObject, submodule_search_locations) ==
                  sizeof(SlotObject) +
                      ModuleSpecObject::kSubmoduleSearchLocationsSlot *
                          sizeof(Value));
    static_assert(CL_OFFSETOF(ModuleSpecObject, has_location) ==
                  sizeof(SlotObject) +
                      ModuleSpecObject::kHasLocationSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(ModuleSpecObject, parent) ==
                  sizeof(SlotObject) +
                      ModuleSpecObject::kParentSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<ModuleSpecObject>);

    BuiltinClassDefinition make_module_spec_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_MODULE_SPEC_OBJECT_H
