#ifndef CL_CLASS_OBJECT_H
#define CL_CLASS_OBJECT_H

#include "builtin_class_registry.h"
#include "instance.h"
#include "object.h"
#include "owned.h"
#include "owned_typed_value.h"
#include "shape.h"
#include "typed_value.h"
#include "value.h"
#include <cstdint>

namespace cl
{
    struct BuiltinClassMethod
    {
        TValue<String> name;
        Value value;
    };

    class ClassObject : public Object
    {
    public:
        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::ClassObject;
        static constexpr uint32_t kClassSlotClass = 0;
        static constexpr uint32_t kClassSlotName = 1;
        static constexpr uint32_t kClassSlotBases = 2;
        static constexpr uint32_t kClassSlotMro = 3;
        static constexpr uint32_t kClassPredefinedSlotCount = 4;
        static constexpr uint32_t kClassInlineSlotCount = 8;

        ClassObject(BootstrapObjectTag, TValue<String> name,
                    uint32_t factory_default_inline_slot_count,
                    Value base = Value::None(),
                    ShapeFlags class_shape_flags =
                        shape_flag(ShapeFlag::IsClassObject));

        ClassObject(ClassObject *metaclass, TValue<String> name,
                    uint32_t factory_default_inline_slot_count,
                    Value base = Value::None(),
                    ShapeFlags class_shape_flags =
                        shape_flag(ShapeFlag::IsClassObject));

        ClassObject(TValue<String> name,
                    uint32_t factory_default_inline_slot_count,
                    Value base = Value::None(),
                    ShapeFlags class_shape_flags =
                        shape_flag(ShapeFlag::IsClassObject));

        static ClassObject *
        make_builtin_class(TValue<String> name,
                           uint32_t factory_default_inline_slot_count,
                           const BuiltinClassMethod *methods,
                           uint32_t method_count, Value base = Value::None());

        TValue<String> get_name() const { return name; }
        uint32_t get_factory_default_inline_slot_count() const
        {
            return factory_default_inline_slot_count;
        }
        uint32_t get_class_inline_slot_count() const
        {
            return kClassInlineSlotCount;
        }
        Shape *get_shape() const;
        void set_shape(Shape *new_shape);
        Shape *get_initial_shape() const;
        ClassObject *get_base() const;

        Value lookup_class_chain(TValue<String> name) const;

        Value get_own_property(TValue<String> name) const;
        bool define_own_property(TValue<String> name, Value value,
                                 DescriptorFlags descriptor_flags);
        bool set_existing_own_property(TValue<String> name, Value value);
        bool set_own_property(TValue<String> name, Value value);
        bool delete_own_property(TValue<String> name);

        Value read_storage_location(StorageLocation location) const;
        void write_storage_location(StorageLocation location, Value value);

    private:
        Value read_inline_slot(uint32_t slot_idx) const;
        Instance::OverflowSlots *get_overflow_slots() const;
        Instance::OverflowSlots *ensure_overflow_slot(int32_t physical_idx);
        Value make_bases_list() const;
        Value make_mro_list() const;

        MemberTValue<String> name;
        MemberValue base;
        MemberHeapPtr<Shape> initial_shape;
        MemberHeapPtr<Shape> shape;
        MemberHeapPtr<Instance::OverflowSlots> overflow;
        MemberValue class_slots[kClassInlineSlotCount];
        uint32_t factory_default_inline_slot_count;

    public:
        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(ClassObject, name,
                                             5 + kClassInlineSlotCount);
    };

    class VirtualMachine;
    BuiltinClassDefinition make_type_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_CLASS_OBJECT_H
