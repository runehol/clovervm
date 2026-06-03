#ifndef CL_SHAPE_H
#define CL_SHAPE_H

#include "native/native_layout_declarations.h"
#include "object_model/heap_object.h"
#include "object_model/owned.h"
#include "object_model/shape_descriptor.h"
#include "object_model/typed_value.h"
#include "object_model/value.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl
{
    class ClassObject;
    class VirtualMachine;
    enum class ShapeTransitionVerb : uint8_t
    {
        Add,
        Delete,
    };

    enum class ShapeFlag : uint16_t
    {
        None = 0,
        IsClassObject = 1 << 0,
        IsImmutableType = 1 << 1,
        HasCustomGetAttribute = 1 << 2,
        HasCustomGetAttrFallback = 1 << 3,
        HasCustomSetAttribute = 1 << 4,
        HasCustomDelAttribute = 1 << 5,
        DisallowAttributeUpdates = 1 << 6,
        DisallowAttributeAddDelete = 1 << 7,
        IsModuleObject = 1 << 8,
    };

    using ShapeFlags = uint16_t;

    constexpr ShapeFlags shape_flag(ShapeFlag flag)
    {
        return static_cast<ShapeFlags>(flag);
    }

    constexpr bool has_shape_flag(ShapeFlags flags, ShapeFlag flag)
    {
        return (flags & shape_flag(flag)) != 0;
    }

    constexpr ShapeFlags mutable_attribute_shape_flags()
    {
        return shape_flag(ShapeFlag::None);
    }

    constexpr ShapeFlags fixed_attribute_shape_flags()
    {
        return shape_flag(ShapeFlag::DisallowAttributeUpdates) |
               shape_flag(ShapeFlag::DisallowAttributeAddDelete);
    }

    constexpr bool valid_shape_flags(ShapeFlags flags)
    {
        return !has_shape_flag(flags, ShapeFlag::DisallowAttributeUpdates) ||
               has_shape_flag(flags, ShapeFlag::DisallowAttributeAddDelete);
    }

    struct ShapeRootDescriptor
    {
        TValue<String> name;
        DescriptorInfo info;
    };

    class Shape : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout = NativeLayoutId::Shape;

        class Transition
        {
        public:
            Transition(TValue<String> name, ShapeTransitionVerb verb,
                       DescriptorFlags descriptor_flags, Shape *next_shape);

            TValue<String> get_name() const { return name.value(); }
            ShapeTransitionVerb get_verb() const { return verb; }
            DescriptorFlags get_descriptor_flags() const
            {
                return descriptor_flags;
            }
            Shape *get_next_shape() const { return next_shape.extract(); }

        private:
            Owned<TValue<String>> name;
            ShapeTransitionVerb verb;
            DescriptorFlags descriptor_flags;
            OwnedHeapPtr<Shape> next_shape;
        };

        Shape(TValue<ClassObject> class_value, Shape *previous_shape,
              int32_t next_slot_index, uint32_t property_count,
              uint32_t inline_slot_capacity, ShapeFlags shape_flags,
              uint32_t present_count);

        static Shape *make_root_with_single_descriptor(
            TValue<ClassObject> class_value, TValue<String> name,
            DescriptorInfo info, int32_t next_slot_index,
            uint32_t inline_slot_capacity, ShapeFlags shape_flags);
        static Shape *make_immortal_root_with_single_descriptor(
            VirtualMachine *vm, TValue<ClassObject> class_value,
            TValue<String> name, DescriptorInfo info, int32_t next_slot_index,
            uint32_t inline_slot_capacity, ShapeFlags shape_flags);
        static Shape *make_root_with_descriptors(
            TValue<ClassObject> class_value,
            const ShapeRootDescriptor *descriptors, uint32_t descriptor_count,
            int32_t next_slot_index, uint32_t present_count,
            uint32_t inline_slot_capacity, ShapeFlags shape_flags);
        static Shape *make_immortal_root_with_descriptors(
            VirtualMachine *vm, TValue<ClassObject> class_value,
            const ShapeRootDescriptor *descriptors, uint32_t descriptor_count,
            int32_t next_slot_index, uint32_t present_count,
            uint32_t inline_slot_capacity, ShapeFlags shape_flags);

        static size_t size_for(uint32_t property_count)
        {
            return sizeof(Shape) + sizeof(Value) * property_count -
                   sizeof(Value) + sizeof(DescriptorInfo) * property_count;
        }
        static size_t size_for(TValue<ClassObject> class_value,
                               Shape *previous_shape, int32_t next_slot_index,
                               uint32_t property_count,
                               uint32_t inline_slot_capacity,
                               ShapeFlags shape_flags, uint32_t present_count)
        {
            (void)class_value;
            (void)previous_shape;
            (void)next_slot_index;
            (void)inline_slot_capacity;
            (void)shape_flags;
            (void)present_count;
            return size_for(property_count);
        }
        static size_t object_size_in_bytes(const Shape *shape)
        {
            return size_for(shape->property_count());
        }

        ClassObject *get_class() const
        {
            // Hot path: avoid TValue<ClassObject>::extract() here so this
            // header does not need ClassObject to be complete.
            return reinterpret_cast<ClassObject *>(
                class_value.raw_value().as.ptr);
        }
        Shape *get_previous_shape() const;
        int32_t get_next_slot_index() const { return next_slot_index; }
        uint32_t get_inline_slot_count() const;
        uint32_t get_instance_default_inline_slot_count() const
        {
            return inline_slot_capacity;
        }

        uint32_t property_count() const { return property_count_; }
        uint32_t present_count() const { return present_count_; }
        uint32_t latent_count() const
        {
            assert(present_count_ <= property_count_);
            return property_count_ - present_count_;
        }
        ShapeFlags flags() const { return shape_flags; }
        bool has_flag(ShapeFlag flag) const
        {
            return has_shape_flag(shape_flags, flag);
        }
        bool allows_attribute_updates() const
        {
            return !has_flag(ShapeFlag::DisallowAttributeUpdates);
        }
        bool allows_attribute_add_delete() const
        {
            return !has_flag(ShapeFlag::DisallowAttributeAddDelete);
        }
        TValue<String> get_property_name(uint32_t property_idx) const
        {
            assert(property_idx < property_count_);
            return TValue<String>::from_value_unchecked(
                descriptor_names[property_idx]);
        }
        StorageLocation
        get_property_storage_location(uint32_t property_idx) const
        {
            assert(property_idx < property_count_);
            return get_descriptor_info(property_idx).storage_location();
        }
        DescriptorInfo get_descriptor_info(uint32_t property_idx) const
        {
            assert(property_idx < property_count_);
            return descriptor_infos()[property_idx];
        }

        uint32_t transition_count() const { return transitions.size(); }

        int32_t lookup_descriptor_index(TValue<String> name) const;
        DescriptorLookup
        lookup_descriptor_including_latent(TValue<String> name) const;
        StorageLocation resolve_present_property(TValue<String> name) const;
        StorageLocation resolve_own_property(TValue<String> name) const;
        Shape *
        lookup_transition(TValue<String> name, ShapeTransitionVerb verb,
                          DescriptorFlags descriptor_flags =
                              descriptor_flag(DescriptorFlag::None)) const;
        Shape *derive_transition(TValue<String> name, ShapeTransitionVerb verb,
                                 DescriptorFlags descriptor_flags =
                                     descriptor_flag(DescriptorFlag::None));
        Shape *clone_with_flags(ShapeFlags new_shape_flags) const;
        Shape *clone_with_class(TValue<ClassObject> new_class) const;

    private:
        void initialize_root_descriptors(const ShapeRootDescriptor *descriptors,
                                         uint32_t descriptor_count);
        Shape *derive_add_transition(TValue<String> name,
                                     DescriptorFlags descriptor_flags);
        Shape *derive_delete_transition(TValue<String> name);
        DescriptorInfo *descriptor_infos()
        {
            return reinterpret_cast<DescriptorInfo *>(
                &descriptor_names[property_count_]);
        }
        const DescriptorInfo *descriptor_infos() const
        {
            return reinterpret_cast<const DescriptorInfo *>(
                &descriptor_names[property_count_]);
        }

        Shape *previous_shape;
        int32_t next_slot_index;
        uint32_t property_count_;
        uint32_t present_count_;
        uint32_t inline_slot_capacity;
        ShapeFlags shape_flags;
        std::vector<Transition> transitions;
        Member<TValue<ClassObject>> class_value;
        Value descriptor_names[1];

    public:
        static void dealloc(HeapObject *obj);

        CL_DECLARE_CUSTOM_DEALLOC(Shape, dealloc);
        CL_DECLARE_CUSTOM_OBJECT_SIZE(Shape, Shape::object_size_in_bytes);
    };

}  // namespace cl

#endif  // CL_SHAPE_H
