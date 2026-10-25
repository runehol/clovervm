#ifndef CL_ENUMERATE_H
#define CL_ENUMERATE_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"

namespace cl
{
    class ModuleObject;
    class VirtualMachine;

    class EnumerateIterator : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::EnumerateIterator;

        EnumerateIterator(ClassObject *cls, Value _source, Value _index)
            : Object(cls, native_layout), source(_source), next_index(_index)
        {
        }

        Member<Value> source;
        // An exact SMI or BigInt; CLInt currently only represents SMIs.
        Member<Value> next_index;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(EnumerateIterator, Object, 2);
        CL_DECLARE_STATIC_OBJECT_SIZE(EnumerateIterator);
    };

    static_assert(std::is_trivially_destructible_v<EnumerateIterator>);

    BuiltinClassDefinition make_enumerate_class(VirtualMachine *vm);
    void install_enumerate_helpers(VirtualMachine *vm, ModuleObject *module);
    void install_enumerate_python_methods(VirtualMachine *vm,
                                          ModuleObject *module);
}  // namespace cl

#endif  // CL_ENUMERATE_H
