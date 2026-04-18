#ifndef CL_SHAPE_H
#define CL_SHAPE_H

#include "klass.h"
#include "object.h"
#include "owned_typed_value.h"
#include "typed_value.h"
#include "value.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cl
{
    class Shape;
    class ClassObject;
    class OverflowSlots;

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

    class ClassObject : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"Class", nullptr);

        ClassObject(TValue<String> name, uint32_t inline_slot_capacity);

        TValue<String> get_name() const { return name; }
        uint32_t get_inline_slot_capacity() const
        {
            return inline_slot_capacity;
        }
        Shape *get_initial_shape() const;

    private:
        MemberTValue<String> name;
        uint32_t inline_slot_capacity;
        MemberValue initial_shape;

    public:
        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(ClassObject, name, 2);
    };

    class Instance : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"Instance", nullptr);

        Instance(Value cls, Value shape);

        static size_t size_for(uint32_t inline_slot_capacity)
        {
            assert(inline_slot_capacity >= 1);
            return sizeof(Instance) + sizeof(Value) * inline_slot_capacity -
                   sizeof(Value);
        }

        static DynamicLayoutSpec layout_spec_for(Value cls, Value shape);

        Value get_class() const { return cls.as_value(); }
        Shape *get_shape() const;
        OverflowSlots *get_overflow_slots() const;

        Value get_own_property(TValue<String> name) const;
        void set_own_property(TValue<String> name, Value value);
        bool delete_own_property(TValue<String> name);

    private:
        Value read_storage_location(StorageLocation location) const;
        void write_storage_location(StorageLocation location, Value value);
        OverflowSlots *ensure_overflow_slot(int32_t physical_idx);

        MemberValue cls;
        MemberValue shape;
        MemberValue overflow;
        Value inline_slots[1];

    public:
        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(Instance, cls);
    };

    class OverflowSlots : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"OverflowSlots", nullptr);

        OverflowSlots(uint32_t size, uint32_t capacity);

        static size_t size_for(uint32_t capacity)
        {
            return sizeof(OverflowSlots) +
                   sizeof(Value) * std::max<uint32_t>(capacity, 1) -
                   sizeof(Value);
        }

        static DynamicLayoutSpec layout_spec_for(uint32_t size,
                                                 uint32_t capacity)
        {
            return DynamicLayoutSpec{
                round_up_to_16byte_units(size_for(capacity)), capacity};
        }

        uint32_t get_size() const { return size; }
        uint32_t get_capacity() const { return capacity; }
        void set_size(uint32_t new_size)
        {
            assert(new_size <= capacity);
            size = new_size;
        }

        Value get(uint32_t slot_idx) const
        {
            assert(slot_idx < capacity);
            return slots[slot_idx];
        }

        void set(uint32_t slot_idx, Value value);

    private:
        uint32_t size;
        uint32_t capacity;
        Value slots[1];

    public:
        CL_DECLARE_DYNAMIC_LAYOUT_WITH_VALUES(OverflowSlots, slots);
    };

    class Shape : public Object
    {
    public:
        static constexpr Klass klass = Klass(L"Shape", nullptr);

        class PropertyDescriptor
        {
        public:
            PropertyDescriptor(TValue<String> name,
                               StorageLocation storage_location)
                : name(name), storage_location(storage_location)
            {
            }

            TValue<String> get_name() const { return name; }
            StorageLocation get_storage_location() const
            {
                return storage_location;
            }

        private:
            OwnedTValue<String> name;
            StorageLocation storage_location;
        };

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

        Shape(Value owner_class, Value previous_shape, int32_t next_slot_index);

        ClassObject *get_owner_class() const;
        Shape *get_previous_shape() const;
        int32_t get_next_slot_index() const { return next_slot_index; }
        uint32_t get_inline_slot_capacity() const;

        uint32_t property_count() const { return descriptors.size(); }
        TValue<String> get_property_name(uint32_t property_idx) const
        {
            return descriptors[property_idx].get_name();
        }
        StorageLocation
        get_property_storage_location(uint32_t property_idx) const
        {
            return descriptors[property_idx].get_storage_location();
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

        MemberValue owner_class;
        Shape *previous_shape;
        int32_t next_slot_index;
        std::vector<PropertyDescriptor> descriptors;
        std::vector<Transition> transitions;

    public:
        CL_DECLARE_STATIC_LAYOUT_WITH_VALUES(Shape, owner_class, 1);
    };

}  // namespace cl

#endif  // CL_SHAPE_H
