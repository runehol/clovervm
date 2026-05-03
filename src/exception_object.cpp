#include "exception_object.h"

#include "class_object.h"
#include "runtime_helpers.h"
#include "shape.h"
#include "str.h"
#include "thread_state.h"
#include "virtual_machine.h"
#include <string>

namespace cl
{
    StopIterationObject::StopIterationObject(ClassObject *cls, Value value)
        : ExceptionObject(cls, native_layout_id, compact_layout(),
                          interned_string(L"")),
          value(value)
    {
    }

    static void install_exception_instance_root_shape(ClassObject *cls)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> message_name = interned_string(L"message");
        DescriptorFlags class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        DescriptorFlags message_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[ExceptionObject::kInlineSlotCount] = {
            ShapeRootDescriptor{dunder_class_name,
                                DescriptorInfo::make(
                                    StorageLocation{ExceptionObject::kClassSlot,
                                                    StorageKind::Inline},
                                    class_flags)},
            ShapeRootDescriptor{
                message_name, DescriptorInfo::make(
                                  StorageLocation{ExceptionObject::kMessageSlot,
                                                  StorageKind::Inline},
                                  message_flags)},
        };
        cls->install_builtin_instance_root_shape(
            descriptors, ExceptionObject::kInlineSlotCount,
            ExceptionObject::kInlineSlotCount, mutable_attribute_shape_flags());
    }

    static void install_stop_iteration_instance_root_shape(ClassObject *cls)
    {
        TValue<String> dunder_class_name = interned_string(L"__class__");
        TValue<String> message_name = interned_string(L"message");
        TValue<String> value_name = interned_string(L"value");
        DescriptorFlags class_flags =
            descriptor_flag(DescriptorFlag::ReadOnly) |
            descriptor_flag(DescriptorFlag::StableSlot);
        DescriptorFlags attribute_flags =
            descriptor_flag(DescriptorFlag::StableSlot);
        ShapeRootDescriptor descriptors[StopIterationObject::kInlineSlotCount] =
            {
                ShapeRootDescriptor{
                    dunder_class_name,
                    DescriptorInfo::make(
                        StorageLocation{ExceptionObject::kClassSlot,
                                        StorageKind::Inline},
                        class_flags)},
                ShapeRootDescriptor{
                    message_name,
                    DescriptorInfo::make(
                        StorageLocation{ExceptionObject::kMessageSlot,
                                        StorageKind::Inline},
                        attribute_flags)},
                ShapeRootDescriptor{
                    value_name,
                    DescriptorInfo::make(
                        StorageLocation{StopIterationObject::kValueSlot,
                                        StorageKind::Inline},
                        attribute_flags)},
            };
        cls->install_builtin_instance_root_shape(
            descriptors, StopIterationObject::kInlineSlotCount,
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

    TValue<ExceptionObject> make_exception_object(TValue<ClassObject> type,
                                                  TValue<String> message)
    {
        return make_internal_value<ExceptionObject>(type.extract(), message);
    }

    TValue<ExceptionObject> make_exception_object(TValue<ClassObject> type,
                                                  const wchar_t *message)
    {
        return make_exception_object(type, interned_string(message));
    }

    TValue<StopIterationObject>
    make_stop_iteration_object(TValue<ClassObject> type, Value value)
    {
        return make_internal_value<StopIterationObject>(type.extract(), value);
    }

}  // namespace cl
