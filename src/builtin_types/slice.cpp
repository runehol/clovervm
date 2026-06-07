#include "builtin_types/slice.h"
#include "builtin_types/string_builder.h"
#include "builtin_types/tuple.h"
#include "object_model/class_object.h"
#include "object_model/native_function.h"
#include "object_model/shape.h"
#include "object_model/typed_value.h"
#include "runtime/exception_propagation.h"
#include "runtime/thread_state.h"
#include "runtime/virtual_machine.h"
#include <cstddef>

namespace cl
{
    static constexpr DescriptorFlags slice_slot_flags()
    {
        return descriptor_flag(DescriptorFlag::StableSlot) |
               descriptor_flag(DescriptorFlag::ReadOnly);
    }

    static ShapeRootDescriptor make_slice_slot_descriptor(VirtualMachine *vm,
                                                          const wchar_t *name,
                                                          uint32_t slot_index)
    {
        return ShapeRootDescriptor{
            vm->get_or_create_interned_string_value(name),
            DescriptorInfo::make(
                StorageLocation{int32_t(slot_index), StorageKind::Inline},
                slice_slot_flags())};
    }

    static void install_slice_instance_shapes(VirtualMachine *vm,
                                              ClassObject *cls)
    {
        ShapeRootDescriptor descriptors[] = {
            make_slice_slot_descriptor(vm, L"start", Slice::kStartSlot),
            make_slice_slot_descriptor(vm, L"stop", Slice::kStopSlot),
            make_slice_slot_descriptor(vm, L"step", Slice::kStepSlot),
        };

        BuiltinInstanceShapeBuilder(cls, BuiltinInstanceShapeDefaults::None,
                                    Slice::kInlineSlotCount)
            .add_slot(descriptors[0].name, Slice::kStartSlot,
                      slice_slot_flags())
            .add_slot(descriptors[1].name, Slice::kStopSlot, slice_slot_flags())
            .add_slot(descriptors[2].name, Slice::kStepSlot, slice_slot_flags())
            .install(fixed_attribute_shape_flags());
        Shape *step_value_shape = cls->get_instance_root_shape();
        Shape *step_none_shape = Shape::make_root_with_descriptors(
            TValue<ClassObject>::from_oop(cls), descriptors,
            std::size(descriptors), step_value_shape->get_next_slot_index(),
            step_value_shape->present_count(),
            step_value_shape->get_inline_slot_count(),
            fixed_attribute_shape_flags());
        vm->install_slice_shapes(cls, step_none_shape, step_value_shape);
    }

    TValue<Slice> make_slice(ThreadState *thread, Value start, Value stop,
                             Value step)
    {
        VirtualMachine *vm = thread->get_machine();
        TValue<Slice> slice =
            thread->make_object_value<Slice>(start, stop, step);
        Shape *shape = step.is_none() ? vm->slice_step_none_shape()
                                      : vm->slice_step_value_shape();
        slice.extract()->set_shape(shape);
        return slice;
    }

    static Expected<int64_t> slice_field_to_smi(Value value)
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

    Expected<NormalizedSlice>
    normalize_slice_for_length(ThreadState *thread, TValue<Slice> slice,
                               int64_t sequence_length)
    {
        (void)thread;
        if(sequence_length < 0)
        {
            return Expected<NormalizedSlice>::raise_exception(
                L"ValueError", L"length should not be negative");
        }

        Slice *raw_slice = slice.extract();
        int64_t step = 1;
        if(!raw_slice->step.raw_value().is_none())
        {
            step = CL_TRY(slice_field_to_smi(raw_slice->step));
            if(step == 0)
            {
                return Expected<NormalizedSlice>::raise_exception(
                    L"ValueError", L"slice step cannot be zero");
            }
        }

        int64_t start = 0;
        if(raw_slice->start.raw_value().is_none())
        {
            start = step < 0 ? sequence_length - 1 : 0;
        }
        else
        {
            start = CL_TRY(slice_field_to_smi(raw_slice->start));
            if(start < 0)
            {
                start += sequence_length;
            }
            if(start < 0)
            {
                start = step < 0 ? -1 : 0;
            }
            else if(start >= sequence_length)
            {
                start = step < 0 ? sequence_length - 1 : sequence_length;
            }
        }

        int64_t stop = 0;
        if(raw_slice->stop.raw_value().is_none())
        {
            stop = step < 0 ? -1 : sequence_length;
        }
        else
        {
            stop = CL_TRY(slice_field_to_smi(raw_slice->stop));
            if(stop < 0)
            {
                stop += sequence_length;
            }
            if(stop < 0)
            {
                stop = step < 0 ? -1 : 0;
            }
            else if(stop >= sequence_length)
            {
                stop = step < 0 ? sequence_length - 1 : sequence_length;
            }
        }

        size_t selected_sequence_length = 0;
        if(step < 0)
        {
            if(stop < start)
            {
                selected_sequence_length =
                    static_cast<size_t>((start - stop - 1) / (-step) + 1);
            }
        }
        else if(start < stop)
        {
            selected_sequence_length =
                static_cast<size_t>((stop - start - 1) / step + 1);
        }

        return Expected<NormalizedSlice>::ok(
            NormalizedSlice{start, stop, step, selected_sequence_length});
    }

    static Value native_slice_new(ThreadState *thread, Value cls_value,
                                  Value args_value)
    {
        VirtualMachine *vm = thread->get_machine();
        if(cls_value != Value::from_oop(vm->slice_class()))
        {
            return thread->set_pending_builtin_exception_string(
                L"TypeError", L"slice.__new__ expects slice as cls");
        }
        TValue<Tuple> args = CL_TRY(TValue<Tuple>::from_value_or_raise(
            args_value, L"TypeError", L"slice.__new__ expects argument tuple"));
        switch(args.extract()->size())
        {
            case 1:
                return make_slice(thread, Value::None(),
                                  args.extract()->item_unchecked(0),
                                  Value::None())
                    .raw_value();
            case 2:
                return make_slice(thread, args.extract()->item_unchecked(0),
                                  args.extract()->item_unchecked(1),
                                  Value::None())
                    .raw_value();
            case 3:
                return make_slice(thread, args.extract()->item_unchecked(0),
                                  args.extract()->item_unchecked(1),
                                  args.extract()->item_unchecked(2))
                    .raw_value();
            default:
                return thread->set_pending_builtin_exception_string(
                    L"TypeError",
                    args.extract()->size() == 0
                        ? L"slice expected at least 1 argument, got 0"
                        : L"slice expected at most 3 arguments");
        }
    }

    static Value native_slice_repr(ThreadState *thread, Value self)
    {
        (void)thread;
        TValue<Slice> slice = CL_TRY(TValue<Slice>::from_value_or_raise(
            self, L"TypeError", L"slice.__repr__ expects a slice receiver"));
        StringBuilder builder;
        builder.append_c_str(L"slice(");
        CL_PROPAGATE_EXCEPTION(builder.append_repr(slice.extract()->start));
        builder.append_c_str(L", ");
        CL_PROPAGATE_EXCEPTION(builder.append_repr(slice.extract()->stop));
        builder.append_c_str(L", ");
        CL_PROPAGATE_EXCEPTION(builder.append_repr(slice.extract()->step));
        builder.append_char(L')');
        return builder.finish();
    }

    static Value native_slice_indices(ThreadState *thread, Value self,
                                      Value length_value)
    {
        TValue<Slice> slice = CL_TRY(TValue<Slice>::from_value_or_raise(
            self, L"TypeError", L"slice.indices expects a slice receiver"));
        int64_t sequence_length = CL_TRY(slice_field_to_smi(length_value));
        NormalizedSlice normalized =
            CL_TRY(normalize_slice_for_length(thread, slice, sequence_length));

        TValue<Tuple> result = thread->make_object_value<Tuple>(3);
        result.extract()->initialize_item_unchecked(
            0, Value::from_smi(normalized.start));
        result.extract()->initialize_item_unchecked(
            1, Value::from_smi(normalized.stop));
        result.extract()->initialize_item_unchecked(
            2, Value::from_smi(normalized.step));
        return result.raw_value();
    }

    BuiltinClassDefinition make_slice_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Slice};
        ClassObject *cls = ClassObject::make_builtin_class<Slice>(
            vm->get_or_create_interned_string_value(L"slice"),
            Slice::kInlineSlotCount, nullptr, 0, vm->object_class());
        install_slice_instance_shapes(vm, cls);
        return builtin_class_definition(cls, native_layout_ids,
                                        BuiltinsVisibility::Public);
    }

    void install_slice_class_methods(VirtualMachine *vm)
    {
        BuiltinIntrinsicMethod methods[] = {
            with_varargs(builtin_intrinsic_method(L"__new__", native_slice_new,
                                                  L"Create a slice object.")),
            builtin_intrinsic_method(L"__repr__", native_slice_repr,
                                     L"Return repr(self)."),
            builtin_intrinsic_method(L"indices", native_slice_indices,
                                     L"Return normalized slice indices."),
        };
        unwrap_bootstrap_expected(
            vm,
            install_builtin_intrinsic_methods(vm, vm->slice_class(), methods,
                                              std::size(methods)),
            "installing intrinsic methods");
    }

}  // namespace cl
