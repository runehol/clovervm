#ifndef CL_EXTENSION_HANDLE_H
#define CL_EXTENSION_HANDLE_H

#include "object_model/refcount.h"
#include "object_model/value.h"

#include <bit>
#include <clovervm/native_module.h>
#include <cstddef>
#include <cstdint>

namespace cl
{
    class ThreadState;

    namespace native_handle_detail
    {
        inline constexpr bool cl_indirect_handles = true;
        inline constexpr size_t frame_handle_cell_count = 8;
    }  // namespace native_handle_detail
}  // namespace cl

struct clover_context
{
    cl::ThreadState *thread;
    cl::Value *handle_chunk_next;
    cl::Value *handle_chunk_end;
    // Only selects deferred-refcount store ownership. Remove with refcounting.
    bool handle_chunk_is_overflow;
};

namespace cl
{
    class HandleChunk final : public HeapObject
    {
    public:
        static constexpr NativeLayoutId native_layout =
            NativeLayoutId::HandleChunk;
        static constexpr size_t CellCount = 32;

        HandleChunk() : HeapObject(native_layout)
        {
            for(Value &slot: slots)
            {
                slot = Value::not_present();
            }
        }

        Value slots[CellCount];

        CL_DECLARE_STATIC_VALUE_SPAN(HandleChunk, slots, CellCount);
        CL_DECLARE_STATIC_OBJECT_SIZE(HandleChunk);
    };

    namespace native_handle_detail
    {
        void advance_handle_chunk_slow(clover_context *ctx);

        ALWAYSINLINE clover_handle direct_handle_from_value(Value value)
        {
            static_assert(sizeof(clover_handle) == sizeof(value.as.integer));
            return std::bit_cast<clover_handle>(value.as.integer);
        }

        ALWAYSINLINE Value direct_value_from_handle(clover_handle handle)
        {
            Value result;
            static_assert(sizeof(handle) == sizeof(result.as.integer));
            result.as.integer =
                std::bit_cast<decltype(result.as.integer)>(handle);
            return result;
        }
    }  // namespace native_handle_detail

    ALWAYSINLINE clover_handle handle_from_rooted_slot(clover_context *ctx,
                                                       Value *slot)
    {
        assert(ctx != nullptr);
        assert(slot != nullptr);
        if constexpr(native_handle_detail::cl_indirect_handles)
        {
            return reinterpret_cast<uintptr_t>(slot);
        }
        else
        {
            return native_handle_detail::direct_handle_from_value(*slot);
        }
    }

    ALWAYSINLINE clover_handle allocate_handle(clover_context *ctx, Value value)
    {
        assert(ctx != nullptr);
        if constexpr(native_handle_detail::cl_indirect_handles)
        {
            assert(ctx->handle_chunk_next < ctx->handle_chunk_end);
            Value *slot = ctx->handle_chunk_next++;
            assert(slot->is_not_present());
            if(ctx->handle_chunk_is_overflow)
            {
                incref(value);
            }
            *slot = value;
            if(ctx->handle_chunk_next == ctx->handle_chunk_end)
            {
                native_handle_detail::advance_handle_chunk_slow(ctx);
            }
            return reinterpret_cast<uintptr_t>(slot);
        }
        else
        {
            return native_handle_detail::direct_handle_from_value(value);
        }
    }

    ALWAYSINLINE Value resolve_handle(clover_handle handle)
    {
        if constexpr(native_handle_detail::cl_indirect_handles)
        {
            return *reinterpret_cast<Value *>(handle);
        }
        else
        {
            return native_handle_detail::direct_value_from_handle(handle);
        }
    }

    inline clover_context make_extension_context(ThreadState *thread, Value *fp)
    {
        assert(thread != nullptr);
        if constexpr(native_handle_detail::cl_indirect_handles)
        {
            Value *begin =
                fp - int32_t(native_handle_detail::frame_handle_cell_count);
            for(size_t idx = 0;
                idx < native_handle_detail::frame_handle_cell_count; ++idx)
            {
                begin[idx] = Value::not_present();
            }
            return clover_context{
                thread, begin,
                begin + native_handle_detail::frame_handle_cell_count - 1,
                false};
        }
        else
        {
            return clover_context{thread, nullptr, nullptr, false};
        }
    }

    class NativeHandleRootRegion
    {
    public:
        explicit NativeHandleRootRegion(ThreadState *thread);
        ~NativeHandleRootRegion();

        NativeHandleRootRegion(const NativeHandleRootRegion &) = delete;
        NativeHandleRootRegion &
        operator=(const NativeHandleRootRegion &) = delete;

        clover_context make_context() const
        {
            if constexpr(native_handle_detail::cl_indirect_handles)
            {
                return clover_context{thread, begin, end, false};
            }
            else
            {
                return clover_context{thread, nullptr, nullptr, false};
            }
        }

    private:
        ThreadState *thread;
        Value *previous_frontier;
        Value *previous_lowest_live_stack_slot;
        Value previous_accumulator_or_not_present;
        Value *begin;
        Value *end;
    };
}  // namespace cl

#endif  // CL_EXTENSION_HANDLE_H
