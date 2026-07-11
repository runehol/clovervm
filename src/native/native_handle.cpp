#include "native/native_handle.h"

#include "runtime/thread_state.h"

namespace cl
{
    namespace native_handle_detail
    {
        void advance_handle_chunk_slow(clover_context *ctx)
        {
            assert(ctx != nullptr);
            assert(ctx->thread != nullptr);
            assert(ctx->handle_chunk_next == ctx->handle_chunk_end);

            HandleChunk *chunk = ctx->thread->make_internal_raw<HandleChunk>();
            Value chunk_value;
            chunk_value.as.ptr = chunk;
            if(ctx->handle_chunk_is_overflow)
            {
                incref(chunk_value);
            }
            *ctx->handle_chunk_end = chunk_value;

            ctx->handle_chunk_next = &chunk->slots[0];
            ctx->handle_chunk_end = &chunk->slots[HandleChunk::CellCount - 1];
            ctx->handle_chunk_is_overflow = true;
        }
    }  // namespace native_handle_detail

    NativeHandleRootRegion::NativeHandleRootRegion(ThreadState *_thread)
        : thread(_thread), previous_frontier(thread->clover_frame_frontier()),
          previous_lowest_live_stack_slot(
              thread->safepoint_scan_record().lowest_live_stack_slot),
          previous_accumulator_or_not_present(
              thread->safepoint_scan_record().accumulator_or_not_present),
          begin(nullptr), end(nullptr)
    {
        assert(thread != nullptr);
        if constexpr(native_handle_detail::cl_indirect_handles)
        {
            begin = previous_frontier -
                    int32_t(native_handle_detail::frame_handle_cell_count);
            end = previous_frontier - 1;
            assert(begin >= thread->clover_stack_begin());
            for(Value *slot = begin; slot <= end; ++slot)
            {
                *slot = Value::not_present();
            }
            thread->set_clover_frame_frontier(begin);
            thread->publish_safepoint_scan_record(
                begin, previous_accumulator_or_not_present);
        }
    }

    NativeHandleRootRegion::~NativeHandleRootRegion()
    {
        if constexpr(native_handle_detail::cl_indirect_handles)
        {
            thread->set_clover_frame_frontier(previous_frontier);
            thread->publish_safepoint_scan_record(
                previous_lowest_live_stack_slot,
                previous_accumulator_or_not_present);
        }
    }
}  // namespace cl
