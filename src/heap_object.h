#ifndef CL_HEAP_OBJECT_H
#define CL_HEAP_OBJECT_H

#include "native_layout_id.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cl
{
    enum class HeapLifecycleState : uint8_t
    {
        Normal,
        InZct,
        Reclaiming,
        Dead,
    };

    /*
      Base class for all VM heap records. HeapObjects have the common header
      needed for refcounting and value scanning, but are not necessarily
      Python-visible objects.
    */
    class HeapObject
    {
    public:
        explicit HeapObject(NativeLayoutId _native_layout_id,
                            uint16_t _native_layout_aux_count = 0)
            : refcount(0), lifecycle_state(HeapLifecycleState::Normal),
              native_layout_id_(_native_layout_id),
              native_layout_aux_count(_native_layout_aux_count)
        {
            assert(native_layout_id_ != NativeLayoutId::Invalid);
        }

        HeapObject()
            : refcount(0), lifecycle_state(HeapLifecycleState::Normal),
              native_layout_id_(NativeLayoutId::Invalid),
              native_layout_aux_count(0)
        {
        }

        NativeLayoutId native_layout_id() const { return native_layout_id_; }
        uint16_t native_layout_aux_count_value() const
        {
            return native_layout_aux_count;
        }
        void set_native_layout_aux_count(uint16_t count)
        {
            native_layout_aux_count = count;
        }

        int32_t refcount;
        HeapLifecycleState lifecycle_state;
        NativeLayoutId native_layout_id_;
        uint16_t native_layout_aux_count;
    };

    static_assert(sizeof(HeapObject) == 8);
    static_assert(std::is_trivially_destructible_v<HeapObject>);

}  // namespace cl

#endif  // CL_HEAP_OBJECT_H
