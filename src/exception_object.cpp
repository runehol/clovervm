#include "exception_object.h"

#include "class_object.h"
#include "runtime_helpers.h"
#include "shape.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <iterator>
#include <string>

namespace cl
{
    StopIterationObject::StopIterationObject(ClassObject *cls, Value value)
        : StopIterationObject(cls, interned_string(L""), value)
    {
    }

    StopIterationObject::StopIterationObject(ClassObject *cls,
                                             TValue<String> message,
                                             Value value)
        : ExceptionObject(cls, native_layout, message), value(value)
    {
    }

    static void install_exception_instance_root_shape(ClassObject *cls)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> message_name = interned_string(L"message");
        DescriptorFlags class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot) |
            descriptor_flag(DescriptorFlag::ShapeClassValue);
        DescriptorFlags message_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[] = {
            ShapeRootDescriptor{dunder_class_name,
                                DescriptorInfo::make(
                                    StorageLocation::not_found(), class_flags)},
            ShapeRootDescriptor{
                message_name, DescriptorInfo::make(
                                  StorageLocation{ExceptionObject::kMessageSlot,
                                                  StorageKind::Inline},
                                  message_flags)},
        };
        cls->install_builtin_instance_root_shape(
            descriptors, std::size(descriptors),
            ExceptionObject::kInlineSlotCount, mutable_attribute_shape_flags());
    }

    static void install_stop_iteration_instance_root_shape(ClassObject *cls)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> message_name = interned_string(L"message");
        TValue<String> value_name = interned_string(L"value");
        DescriptorFlags class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot) |
            descriptor_flag(DescriptorFlag::ShapeClassValue);
        DescriptorFlags attribute_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[] = {
            ShapeRootDescriptor{dunder_class_name,
                                DescriptorInfo::make(
                                    StorageLocation::not_found(), class_flags)},
            ShapeRootDescriptor{
                message_name, DescriptorInfo::make(
                                  StorageLocation{ExceptionObject::kMessageSlot,
                                                  StorageKind::Inline},
                                  attribute_flags)},
            ShapeRootDescriptor{
                value_name, DescriptorInfo::make(
                                StorageLocation{StopIterationObject::kValueSlot,
                                                StorageKind::Inline},
                                attribute_flags)},
        };
        cls->install_builtin_instance_root_shape(
            descriptors, std::size(descriptors),
            StopIterationObject::kInlineSlotCount,
            mutable_attribute_shape_flags());
    }

    static ClassObject *make_exception_class_raw(VirtualMachine *vm,
                                                 const wchar_t *name,
                                                 ClassObject *base)
    {
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(name),
            ExceptionObject::kInlineSlotCount, nullptr, 0, base);
        install_exception_instance_root_shape(cls);
        return cls;
    }

    BuiltinClassDefinition make_base_exception_class(VirtualMachine *vm)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::Exception};
        ClassObject *cls =
            make_exception_class_raw(vm, L"BaseException", vm->object_class());
        return builtin_class_definition(cls, native_layout_ids);
    }

    BuiltinClassDefinition make_exception_class(VirtualMachine *vm,
                                                ClassObject *base)
    {
        return builtin_class_definition(
            make_exception_class_raw(vm, L"Exception", base));
    }

    BuiltinClassDefinition make_exception_subclass(VirtualMachine *vm,
                                                   const wchar_t *name,
                                                   ClassObject *base)
    {
        return builtin_class_definition(
            make_exception_class_raw(vm, name, base));
    }

    BuiltinClassDefinition make_stop_iteration_class(VirtualMachine *vm,
                                                     ClassObject *base)
    {
        static constexpr NativeLayoutId native_layout_ids[] = {
            NativeLayoutId::StopIteration};
        ClassObject *cls = ClassObject::make_builtin_class(
            vm->get_or_create_interned_string_value(L"StopIteration"),
            StopIterationObject::kInlineSlotCount, nullptr, 0, base);
        install_stop_iteration_instance_root_shape(cls);
        return builtin_class_definition(cls, native_layout_ids);
    }

    TValue<Exception> make_exception_object(TValue<ClassObject> type,
                                            TValue<String> message)
    {
        return TValue<Exception>::from_value_unchecked(
            make_internal_value<ExceptionObject>(type.extract(), message)
                .raw_value());
    }

    TValue<Exception> make_exception_object(ThreadState *thread,
                                            TValue<ClassObject> type,
                                            TValue<String> message)
    {
        return TValue<Exception>::from_value_unchecked(
            thread
                ->make_internal_value<ExceptionObject>(type.extract(), message)
                .raw_value());
    }

    TValue<Exception> make_exception_object(TValue<ClassObject> type,
                                            const wchar_t *message)
    {
        return make_exception_object(type, interned_string(message));
    }

    TValue<Exception> make_exception_object(ThreadState *thread,
                                            TValue<ClassObject> type,
                                            const wchar_t *message)
    {
        return make_exception_object(
            thread, type,
            thread->get_machine()->get_or_create_interned_string_value(
                message));
    }

    TValue<StopIterationObject>
    make_stop_iteration_object(TValue<ClassObject> type, Value value)
    {
        return TValue<StopIterationObject>::from_value_unchecked(
            make_internal_value<StopIterationObject>(type.extract(), value)
                .raw_value());
    }

    TValue<StopIterationObject>
    make_stop_iteration_object(ThreadState *thread, TValue<ClassObject> type,
                               Value value)
    {
        return TValue<StopIterationObject>::from_value_unchecked(
            thread
                ->make_internal_value<StopIterationObject>(
                    type.extract(),
                    thread->get_machine()->get_or_create_interned_string_value(
                        L""),
                    value)
                .raw_value());
    }

}  // namespace cl
