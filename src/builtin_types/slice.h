#ifndef CL_SLICE_H
#define CL_SLICE_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/value.h"
#include <type_traits>

namespace cl
{
    class ThreadState;
    class VirtualMachine;

    class Slice : public SlotObject
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Slice;
        static constexpr uint32_t kStartSlot = 0;
        static constexpr uint32_t kStopSlot = 1;
        static constexpr uint32_t kStepSlot = 2;
        static constexpr uint32_t kInlineSlotCount = 3;

        Slice(ClassObject *cls, Value _start, Value _stop, Value _step)
            : SlotObject(cls, native_layout), start(_start), stop(_stop),
              step(_step)
        {
        }

        Member<Value> start;
        Member<Value> stop;
        Member<Value> step;

        CL_DECLARE_STATIC_VALUE_SPAN_EXTENDS(Slice, SlotObject, 3);
        CL_DECLARE_STATIC_OBJECT_SIZE(Slice);
    };

    static_assert(CL_OFFSETOF(Slice, start) ==
                  sizeof(SlotObject) + Slice::kStartSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(Slice, stop) ==
                  sizeof(SlotObject) + Slice::kStopSlot * sizeof(Value));
    static_assert(CL_OFFSETOF(Slice, step) ==
                  sizeof(SlotObject) + Slice::kStepSlot * sizeof(Value));
    static_assert(std::is_trivially_destructible_v<Slice>);

    [[nodiscard]] TValue<Slice> make_slice(ThreadState *thread, Value start,
                                           Value stop, Value step);

    BuiltinClassDefinition make_slice_class(VirtualMachine *vm);
    void install_slice_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_SLICE_H
