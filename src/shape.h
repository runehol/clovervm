#ifndef CL_SHAPE_H
#define CL_SHAPE_H

#include "klass.h"
#include "object.h"
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

    class Shape : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"Shape", nullptr);

        class Transition
        {
        public:
            Transition(TValue<String> name, ShapeTransitionVerb verb,
                       Shape *next_shape)
                : name(name), verb(verb),
                  next_shape(TValue<Shape>::from_oop(next_shape))
            {
            }

            TValue<String> get_name() const { return name; }
            ShapeTransitionVerb get_verb() const { return verb; }
            Shape *get_next_shape() const { return next_shape.extract(); }

        private:
            OwnedTValue<String> name;
            ShapeTransitionVerb verb;
            OwnedTValue<Shape> next_shape;
        };

        Shape(Value owner_class, Value previous_shape, int32_t next_slot_index,
              uint32_t property_count);

        static size_t size_for(uint32_t property_count)
        {
            return sizeof(Shape) + sizeof(Value) * property_count -
                   sizeof(Value) + sizeof(StorageLocation) * property_count;
        }

        static DynamicLayoutSpec layout_spec_for(Value owner_class,
                                                 Value previous_shape,
                                                 int32_t next_slot_index,
                                                 uint32_t property_count)
        {
            return DynamicLayoutSpec{
                round_up_to_16byte_units(size_for(property_count)),
                uint64_t(1 + property_count)};
        }

        ClassObject *get_owner_class() const;
        Shape *get_previous_shape() const;
        int32_t get_next_slot_index() const { return next_slot_index; }
        uint32_t get_inline_slot_capacity() const;

        uint32_t property_count() const { return property_count_; }
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
            return descriptor_storage_locations()[property_idx];
        }

        uint32_t transition_count() const { return transitions.size(); }

        int32_t lookup_property_index(TValue<String> name) const;
        StorageLocation resolve_own_property(TValue<String> name) const;
        Shape *lookup_transition(TValue<String> name,
                                 ShapeTransitionVerb verb) const;
        Shape *derive_transition(TValue<String> name, ShapeTransitionVerb verb);

    private:
        Shape *derive_add_transition(TValue<String> name);
        Shape *derive_delete_transition(TValue<String> name);
        StorageLocation *descriptor_storage_locations()
        {
            return reinterpret_cast<StorageLocation *>(
                &descriptor_names[property_count_]);
        }
        const StorageLocation *descriptor_storage_locations() const
        {
            return reinterpret_cast<const StorageLocation *>(
                &descriptor_names[property_count_]);
        }

        Shape *previous_shape;
        int32_t next_slot_index;
        uint32_t property_count_;
        std::vector<Transition> transitions;
        MemberValue owner_class;
        Value descriptor_names[1];

    public:
        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(Shape, owner_class);
    };

}  // namespace cl

#endif  // CL_SHAPE_H
