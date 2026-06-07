#ifndef CL_SLICE_H
#define CL_SLICE_H

#include "object_model/builtin_class_registry.h"
#include "object_model/object.h"
#include "object_model/owned.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include <cstddef>
#include <cstdint>
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

    struct NormalizedNonstridedSlice
    {
        int64_t start;
        size_t selected_sequence_length;
    };

    struct NormalizedGeneralSlice
    {
        int64_t start;
        int64_t stop;
        int64_t step;
        size_t selected_sequence_length;
    };

    ALWAYSINLINE Expected<int64_t> slice_field_to_smi(Value value)
    {
        if(!value.is_smi())
        {
            return Expected<int64_t>::raise_exception(
                L"TypeError",
                L"slice indices must be integers or None or have an "
                L"__index__ method");
        }
        return Expected<int64_t>::ok(value.get_smi());
    }

    ALWAYSINLINE int64_t normalize_slice_field(int64_t index,
                                               int64_t sequence_length,
                                               int64_t lower, int64_t upper)
    {
        if(index < 0)
        {
            index += sequence_length;
        }
        if(index < lower)
        {
            return lower;
        }
        if(index > upper)
        {
            return upper;
        }
        return index;
    }

    ALWAYSINLINE size_t selected_sequence_length_for_general_slice(
        int64_t start, int64_t stop, int64_t step)
    {
        if(step < 0)
        {
            return stop < start
                       ? static_cast<size_t>((start - stop - 1) / (-step) + 1)
                       : 0;
        }
        return start < stop ? static_cast<size_t>((stop - start - 1) / step + 1)
                            : 0;
    }

    [[nodiscard]] TValue<Slice> make_slice(ThreadState *thread, Value start,
                                           Value stop, Value step);
    [[nodiscard]] ALWAYSINLINE Expected<NormalizedNonstridedSlice>
    normalize_nonstrided_slice_for_length(ThreadState *thread,
                                          TValue<Slice> slice,
                                          int64_t sequence_length)
    {
        (void)thread;
        Slice *raw_slice = slice.extract();
        int64_t start = raw_slice->start.raw_value().is_none()
                            ? 0
                            : normalize_slice_field(
                                  CL_TRY(slice_field_to_smi(raw_slice->start)),
                                  sequence_length, 0, sequence_length);
        int64_t stop = raw_slice->stop.raw_value().is_none()
                           ? sequence_length
                           : normalize_slice_field(
                                 CL_TRY(slice_field_to_smi(raw_slice->stop)),
                                 sequence_length, 0, sequence_length);
        size_t selected_sequence_length =
            start < stop ? static_cast<size_t>(stop - start) : 0;
        return Expected<NormalizedNonstridedSlice>::ok(
            NormalizedNonstridedSlice{start, selected_sequence_length});
    }

    [[nodiscard]] ALWAYSINLINE Expected<NormalizedGeneralSlice>
    normalize_general_slice_for_length(ThreadState *thread, TValue<Slice> slice,
                                       int64_t sequence_length)
    {
        (void)thread;
        Slice *raw_slice = slice.extract();
        int64_t step = 1;
        if(!raw_slice->step.raw_value().is_none())
        {
            step = CL_TRY(slice_field_to_smi(raw_slice->step));
            if(step == 0)
            {
                return Expected<NormalizedGeneralSlice>::raise_exception(
                    L"ValueError", L"slice step cannot be zero");
            }
        }

        int64_t lower = step < 0 ? -1 : 0;
        int64_t upper = step < 0 ? sequence_length - 1 : sequence_length;
        int64_t start = raw_slice->start.raw_value().is_none()
                            ? (step < 0 ? sequence_length - 1 : 0)
                            : normalize_slice_field(
                                  CL_TRY(slice_field_to_smi(raw_slice->start)),
                                  sequence_length, lower, upper);
        int64_t stop = raw_slice->stop.raw_value().is_none()
                           ? (step < 0 ? -1 : sequence_length)
                           : normalize_slice_field(
                                 CL_TRY(slice_field_to_smi(raw_slice->stop)),
                                 sequence_length, lower, upper);
        size_t selected_sequence_length =
            selected_sequence_length_for_general_slice(start, stop, step);
        return Expected<NormalizedGeneralSlice>::ok(NormalizedGeneralSlice{
            start, stop, step, selected_sequence_length});
    }

    BuiltinClassDefinition make_slice_class(VirtualMachine *vm);
    void install_slice_class_methods(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_SLICE_H
