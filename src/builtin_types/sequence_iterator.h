#ifndef CL_SEQUENCE_ITERATOR_H
#define CL_SEQUENCE_ITERATOR_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/value.h"
#include <cstdint>

namespace cl
{
    class VirtualMachine;
    class ModuleObject;

    class SequenceIterator : public Object
    {
    public:
        static constexpr uint32_t kPredefinedInlineSlotCount = 0;
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::SequenceIterator;
        SequenceIterator(ClassObject *cls, Value value)
            : Object(cls, native_layout), source(value), index(0)
        {
        }

        Member<Value> source;  // not_present means permanently exhausted.
        int64_t index;
        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(SequenceIterator, Object, 1);
        CL_DECLARE_STATIC_OBJECT_SIZE(SequenceIterator);
    };
    static_assert(std::is_trivially_destructible_v<SequenceIterator>);

    BuiltinClassDefinition make_sequence_iterator_class(VirtualMachine *vm);
    void install_sequence_iterator_bindings(VirtualMachine *vm,
                                            ModuleObject *module);
    void install_sequence_iterator_python_methods(VirtualMachine *vm,
                                                  ModuleObject *module);
}  // namespace cl
#endif
