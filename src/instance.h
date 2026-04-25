#ifndef CL_INSTANCE_H
#define CL_INSTANCE_H

#include "builtin_class_registry.h"
#include "object.h"
#include "shape.h"
#include "typed_value.h"
#include "value.h"
#include <algorithm>
#include <cstdint>

namespace cl
{
    class VirtualMachine;

    class Instance : public Object
    {
    public:
        class OverflowSlots : public HeapObject
        {
        public:
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

        static constexpr NativeLayoutId native_layout_id =
            NativeLayoutId::Instance;

        Instance(Value cls, Shape *shape);

        static size_t size_for(uint32_t dynamic_inline_slot_count)
        {
            return sizeof(Instance) +
                   sizeof(Value) * dynamic_inline_slot_count - sizeof(Value);
        }

        static DynamicLayoutSpec layout_spec_for(Value cls, Shape *shape);

        Value get_class() const;
        Shape *get_shape() const;
        void set_shape(Shape *new_shape);
        OverflowSlots *get_overflow_slots() const;

        Value get_own_property(TValue<String> name) const;
        bool set_own_property(TValue<String> name, Value value);
        bool delete_own_property(TValue<String> name);
        Value read_storage_location(StorageLocation location) const;
        void write_storage_location(StorageLocation location, Value value);

    private:
        OverflowSlots *ensure_overflow_slot(int32_t physical_idx);

        [[maybe_unused]] Value inline_slots[1];

    public:
        static constexpr bool has_dynamic_layout = true;
        static constexpr uint32_t static_value_offset_in_words()
        {
            return Object::static_value_offset_in_words();
        }
    };

    BuiltinClassDefinition make_instance_class(VirtualMachine *vm);

}  // namespace cl

#endif  // CL_INSTANCE_H
