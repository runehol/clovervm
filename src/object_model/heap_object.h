#ifndef CL_HEAP_OBJECT_H
#define CL_HEAP_OBJECT_H

#include "object_model/heap_header.h"

#include <cassert>
#include <cstdint>
#include <type_traits>

namespace cl
{
    // Representation-level operations belong to the contained HeapHeader,
    // never to the complete HeapObject or any of its subclasses.
    class HeapObject
    {
    public:
        HeapObject() : header_(NativeLayoutId::Invalid) {}

        explicit HeapObject(NativeLayoutId layout, uint16_t aux = 0)
            : header_(layout, aux)
        {
            assert(layout != NativeLayoutId::Invalid);
        }

        NativeLayoutId native_layout_id() const
        {
            return header_.native_layout();
        }

        uint16_t native_layout_aux_count_value() const
        {
            return header_.aux_count();
        }

        void set_native_layout_aux_count(uint16_t count)
        {
            header_.set_aux_count(count);
        }

        HeapGenerationState generation_state() const
        {
            return header_.generation();
        }

        void set_generation_state(HeapGenerationState state)
        {
            header_.set_generation(state);
        }

        HeapLifecycleState lifecycle_state() const
        {
            return header_.lifecycle();
        }

        void set_lifecycle_state(HeapLifecycleState state)
        {
            header_.set_lifecycle(state);
        }

        int32_t &refcount() { return header_.refcount(); }
        int32_t refcount() const { return header_.refcount(); }

        bool is_forwarded() const { return header_.is_forwarded(); }

        bool is_non_moving() const { return header_.is_non_moving(); }

        void make_non_moving() { header_.make_non_moving(); }

        HeapObject *forwarding_target() const
        {
            return header_.forwarding_target();
        }

        void install_forwarding_target(HeapObject *target)
        {
            header_.install_forwarding_target(target);
        }

    private:
        HeapHeader header_;
    };

    static_assert(sizeof(HeapObject) == 8);
    static_assert(alignof(HeapObject) == alignof(HeapHeader));
    static_assert(std::is_standard_layout_v<HeapObject>);
    static_assert(std::is_trivially_copyable_v<HeapObject>);
    static_assert(std::is_trivially_destructible_v<HeapObject>);
}  // namespace cl

#endif  // CL_HEAP_OBJECT_H
