#ifndef CL_SHAPE_H
#define CL_SHAPE_H

#include "object.h"
#include "owned.h"
#include "owned_typed_value.h"
#include "typed_value.h"
#include "value.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl
{
    class ClassObject;
    enum class ShapeTransitionVerb : uint8_t
    {
        Add,
        Delete,
    };

    enum class StorageKind : uint8_t
    {
        Inline,
        Overflow,
    };

    struct StorageLocation
    {
        int32_t physical_idx;
        StorageKind kind;

        static StorageLocation not_found()
        {
            return StorageLocation{-1, StorageKind::Inline};
        }

        bool is_found() const { return physical_idx >= 0; }
    };

    enum class DescriptorFlag : uint16_t
    {
        None = 0,
        ReadOnly = 1 << 0,
        StableSlot = 1 << 1,
    };

    using DescriptorFlags = uint16_t;

    constexpr DescriptorFlags descriptor_flag(DescriptorFlag flag)
    {
        return static_cast<DescriptorFlags>(flag);
    }

    constexpr bool has_descriptor_flag(DescriptorFlags flags,
                                       DescriptorFlag flag)
    {
        return (flags & descriptor_flag(flag)) != 0;
    }

    struct DescriptorInfo
    {
        int32_t physical_idx;
        StorageKind kind;
        uint8_t reserved;
        DescriptorFlags flags;

        static DescriptorInfo not_found()
        {
            return DescriptorInfo{-1, StorageKind::Inline, 0,
                                  descriptor_flag(DescriptorFlag::None)};
        }

        static DescriptorInfo
        make(StorageLocation location,
             DescriptorFlags flags = descriptor_flag(DescriptorFlag::None))
        {
            return DescriptorInfo{location.physical_idx, location.kind, 0,
                                  flags};
        }

        StorageLocation storage_location() const
        {
            return StorageLocation{physical_idx, kind};
        }

        bool has_flag(DescriptorFlag flag) const
        {
            return has_descriptor_flag(flags, flag);
        }
    };

    static_assert(sizeof(DescriptorInfo) == 8,
                  "DescriptorInfo should stay packed into 64 bits");

    enum class DescriptorPresence : uint8_t
    {
        Absent,
        Present,
        Latent,
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

    struct DescriptorLookup
    {
        DescriptorPresence presence;
        int32_t descriptor_idx;
        DescriptorInfo info;

        static DescriptorLookup absent()
        {
            return DescriptorLookup{DescriptorPresence::Absent, -1,
                                    DescriptorInfo::not_found()};
        }

        bool is_present() const
        {
            return presence == DescriptorPresence::Present;
        }

        bool is_latent() const
        {
            return presence == DescriptorPresence::Latent;
        }

        StorageLocation storage_location() const
        {
            return info.storage_location();
        }
    };

    struct ShapeRootDescriptor
    {
        TValue<String> name;
        DescriptorInfo info;
    };

    class Shape : public HeapObject
    {
    public:
        class Transition
        {
        public:
            Transition(TValue<String> name, ShapeTransitionVerb verb,
                       DescriptorFlags descriptor_flags, Shape *next_shape);

            TValue<String> get_name() const { return name; }
            ShapeTransitionVerb get_verb() const { return verb; }
            DescriptorFlags get_descriptor_flags() const
            {
                return descriptor_flags;
            }
            Shape *get_next_shape() const { return next_shape.extract(); }

        private:
            OwnedTValue<String> name;
            ShapeTransitionVerb verb;
            DescriptorFlags descriptor_flags;
            OwnedHeapPtr<Shape> next_shape;
        };

        Shape(Value owner_class, Shape *previous_shape, int32_t next_slot_index,
              uint32_t property_count);
        Shape(Value owner_class, Shape *previous_shape, int32_t next_slot_index,
              uint32_t property_count, ShapeFlags shape_flags);
        Shape(Value owner_class, Shape *previous_shape, int32_t next_slot_index,
              uint32_t property_count, ShapeFlags shape_flags,
              uint32_t present_count);

        static Shape *make_root_with_single_descriptor(
            Value owner_class, TValue<String> name, DescriptorInfo info,
            int32_t next_slot_index,
            ShapeFlags shape_flags = shape_flag(ShapeFlag::None));
        static Shape *make_root_with_descriptors(
            Value owner_class, const ShapeRootDescriptor *descriptors,
            uint32_t descriptor_count, int32_t next_slot_index,
            ShapeFlags shape_flags = shape_flag(ShapeFlag::None));

        static size_t size_for(uint32_t property_count)
        {
            return sizeof(Shape) + sizeof(Value) * property_count -
                   sizeof(Value) + sizeof(DescriptorInfo) * property_count;
        }

        static DynamicLayoutSpec layout_spec_for(Value owner_class,
                                                 Shape *previous_shape,
                                                 int32_t next_slot_index,
                                                 uint32_t property_count)
        {
            return layout_spec_for(owner_class, previous_shape, next_slot_index,
                                   property_count, shape_flag(ShapeFlag::None));
        }

        static DynamicLayoutSpec layout_spec_for(Value owner_class,
                                                 Shape *previous_shape,
                                                 int32_t next_slot_index,
                                                 uint32_t property_count,
                                                 ShapeFlags shape_flags)
        {
            return layout_spec_for(owner_class, previous_shape, next_slot_index,
                                   property_count, shape_flags, property_count);
        }

        static DynamicLayoutSpec
        layout_spec_for(Value owner_class, Shape *previous_shape,
                        int32_t next_slot_index, uint32_t property_count,
                        ShapeFlags shape_flags, uint32_t present_count)
        {
            (void)shape_flags;
            (void)present_count;
            return DynamicLayoutSpec{
                round_up_to_16byte_units(size_for(property_count)),
                uint64_t(1 + property_count)};
        }

        ClassObject *get_owner_class() const;
        Shape *get_previous_shape() const;
        int32_t get_next_slot_index() const { return next_slot_index; }
        uint32_t get_inline_slot_count() const;
        uint32_t get_factory_default_inline_slot_count() const;

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
        TValue<String> get_property_name(uint32_t property_idx) const
        {
            assert(property_idx < property_count_);
            return TValue<String>::unsafe_unchecked(
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

    private:
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
        ShapeFlags shape_flags;
        std::vector<Transition> transitions;
        MemberValue owner_class;
        Value descriptor_names[1];

    public:
        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(Shape, owner_class);
    };

}  // namespace cl

#endif  // CL_SHAPE_H
