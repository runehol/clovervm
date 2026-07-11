# Switchable Indirect Native Handles

## Purpose

The CloverVM C API exposes `clover_handle` as an opaque machine word. The VM
should be able to implement that word either as direct `Value` bits or as an
indirect pointer to a rooted `Value` slot:

```cpp
namespace cl::native_handle_detail
{
    inline constexpr bool cl_indirect_handles = true;
}
```

The flag lives in an implementation-detail namespace because it selects one VM
build strategy. Runtime code outside the handle implementation should not branch
on it or acquire separate direct/indirect policies.

Indirect handles make the native boundary compatible with movable managed
objects: a collector rewrites the rooted slot while native code continues to
hold the same opaque handle. Direct handles preserve the current lower-cost
representation if CloverVM does not adopt moving collection. Both modes use the
same public C API and native-module source code.

This design covers handles valid for one active native API call. Persistent
native roots are separate work.

## Public Contract

The public representation remains opaque and pointer-sized:

```c
typedef uintptr_t clover_handle;
```

Native extensions must not inspect, dereference, compare as identity, or retain
the representation after the active API call. Changing
`native_handle_detail::cl_indirect_handles` does not change public function
signatures or the size of `clover_handle`.

Every supported C API call receives a valid `clover_context *` for the active
native frame. Null, stale, or fabricated contexts are outside the API contract;
the handle implementation does not need to manufacture an error handle without
a valid context.

## Internal Handle Operations

The representation operations are small inline functions in the internal
`api/extension_handle.h` header. C API implementation files and extension-call
thunks use this header; it is not installed as part of the public extension
API.

The implementation distinguishes an existing rooted slot from a value that
needs a new root:

```cpp
clover_handle handle_from_rooted_slot(clover_context *ctx, Value *slot);
clover_handle allocate_handle(clover_context *ctx, Value value);

[[nodiscard]] Value resolve_handle(clover_handle handle);
```

`handle_from_rooted_slot` does not allocate. Managed arguments already occupy
stable `Value` slots in the native thunk frame, so their handles use those
slots directly in indirect mode.

`allocate_handle` is used for values produced by C API operations. In indirect
mode it claims a rooted API-storage slot and writes the value there. In direct
mode it encodes the value bits in the handle.

Conceptually:

```cpp
ALWAYSINLINE clover_handle
handle_from_rooted_slot(clover_context *ctx, Value *slot)
{
    if constexpr(native_handle_detail::cl_indirect_handles)
    {
        assert(ctx != nullptr);
        return reinterpret_cast<uintptr_t>(slot);
    }
    else
    {
        return direct_handle_from_value(*slot);
    }
}

ALWAYSINLINE clover_handle
allocate_handle(clover_context *ctx, Value value)
{
    if constexpr(native_handle_detail::cl_indirect_handles)
    {
        assert(ctx->handle_chunk_next < ctx->handle_chunk_end);
        Value *slot = ctx->handle_chunk_next++;
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
        return direct_handle_from_value(value);
    }
}
```

`resolve_handle` follows the same header-inline pattern. With indirect handles
disabled, `if constexpr` removes every reference to context storage, slot
allocation, pointer validation, and overflow helpers. The resulting code is the
direct `Value` bit conversion rather than a runtime abstraction layer.

The indirect allocation fast path remains inline. It stores the value before
the uncommon chunk-allocation path runs, so the value is already rooted if
overflow allocation reaches a safepoint. Filling the last allocatable cell
eagerly prepares the next overflow chunk, even if the native call creates no
further handles.

Only that eager chunk transition needs an out-of-line helper:

```cpp
namespace native_handle_detail
{
    void advance_handle_chunk_slow(clover_context *ctx);
}
```

The allocation helper is infallible at the Python level. Heap allocation may
request a safepoint and reclamation before satisfying the request, but it does
not report allocation failure as a pending Python exception.

Indirect handle resolution can directly reinterpret and dereference the opaque
slot pointer. Debug builds may validate active-frame and chunk ranges to catch
extension misuse, but runtime validation is not part of the supported API
contract. Direct mode must not reference indirect validation or chunk helpers.

The existing generic `wrap_clover_handle(Value)` operation should be removed.
It loses the important distinction between reusing an existing rooted slot and
allocating storage for a new handle.

## Managed Frame Storage

When an extension thunk is generated in indirect mode, its builder reserves
`frame_handle_cell_count` ordinary temporary-register cells. This changes the
recorded frame size of the generated thunk; it is not a dynamic allocation or
reservation performed on each call. Every invocation therefore enters with a
fixed-size API-handle chunk already present on the stable Clover stack. The
storage is separate from the existing argument slots, whose handles continue to
point directly at those slots.

The implementation uses a scoped `CodeObjectBuilder::TemporaryReg` while
emitting the extension call. The C++ object's lifetime is only a code-generation
device: it makes the cells live in the thunk's frame layout across the emitted
call. The thunk does not read or initialize a separate temporary value at run
time. Direct mode omits this reservation entirely.

The final cell is not available for handle allocation. It is a managed link to
the first overflow chunk:

```text
managed native thunk frame
  existing argument slots
    handle(argN) -> &argument_slot[N]
  fixed API-handle chunk
    [handle]
    [handle]
    ...
    [next HandleChunk or not_present]
```

All unused API-handle cells and the initial link cell contain
`Value::not_present()`.

## Overflow Handle Chunks

When the fixed frame chunk fills, allocation continues in a `HandleChunk` heap
object. Every chunk uses the same layout convention: all cells except the final
one are handle storage, and the final cell links to the next chunk.

```cpp
class HandleChunk final : public HeapObject
{
public:
    static constexpr size_t CellCount = 32;

    Value slots[CellCount];
};
```

All cells are initialized to `Value::not_present()`. There is no `used` count.
The context's allocation pointers identify the occupied prefix of the current
chunk, while every earlier chunk is known to be full because a successor is
allocated only after its predecessor fills.

`HandleChunk` is a managed, precisely scannable heap layout. Its descriptor
visits and updates every slot, including the final next-chunk cell. Unused cells
contain `not_present`, occupied cells contain API values, and the final cell
contains the next `HandleChunk` or `not_present`.

The raw `Value` span follows the existing `SlotObject` pattern rather than using
`Member<Value>` for each cell. `HandleChunk` supplies the corresponding native
layout metadata and manages ownership manually. Its release descriptor releases
every cell, including the next-chunk link.

While deferred refcounting remains, storing into an overflow `HandleChunk` cell
must incref the stored value. Storing into fixed managed-frame handle storage
must not incref: those cells use the same stack-root semantics as ordinary
managed frame slots. Reclaiming a `HandleChunk` releases its retained values.
This distinction disappears if tracing GC later replaces refcount ownership.

The chunk itself must use stable, non-moving storage. Indirect handles are
interior `Value *` pointers into the chunk, so moving a `HandleChunk` would
invalidate them even though the collector can update the values stored inside
it.

The frame link roots the first overflow chunk. Each chunk's final cell roots its
successor. The context does not separately own or remember any `HandleChunk`:

```text
managed frame link
  -> HandleChunk
       handle slots
       final link
         -> HandleChunk
              ...
```

When the managed native frame is unwound, its link cell ceases to be a stack
root. The first zero-refcount chunk then becomes reclaimable, and releasing that
chunk's retained cells makes the rest of the chain reclaimable through ordinary
managed ownership.

## Context And Allocation

The context uses the same allocation pointers for fixed frame storage and
overflow storage:

```cpp
struct clover_context
{
    ThreadState *thread;
    Value *handle_chunk_next;
    Value *handle_chunk_end;
    // Only selects deferred-refcount store ownership. Remove with refcounting.
    bool handle_chunk_is_overflow;
};
```

The interval `[handle_chunk_next, handle_chunk_end)` is available handle
storage. `handle_chunk_end` points at the reserved final link cell rather than
one past the complete chunk.

At an ordinary extension-thunk entry, the pointers select the fixed chunk in the
managed frame:

```cpp
ctx.handle_chunk_next = frame_handle_chunk_begin(fp);
ctx.handle_chunk_end = frame_handle_chunk_link(fp);
ctx.handle_chunk_is_overflow = false;
```

Allocation is a bump in the common case. The boolean selects the current
refcount store policy without requiring the context to remember a chunk object.
After the last allocatable cell is filled and rooted, the slow path allocates a
new stable `HandleChunk`, publishes it through the old link cell, and redirects
the same two context pointers to that chunk:

```cpp
void advance_handle_chunk_slow(clover_context *ctx)
{
    assert(ctx->handle_chunk_next == ctx->handle_chunk_end);

    HandleChunk *chunk = allocate_stable_handle_chunk();
    Value chunk_value = Value::from_oop(chunk);
    if(ctx->handle_chunk_is_overflow)
    {
        incref(chunk_value);
    }
    *ctx->handle_chunk_end = chunk_value;

    ctx->handle_chunk_next = &chunk->slots[0];
    ctx->handle_chunk_end =
        &chunk->slots[HandleChunk::CellCount - 1];
    ctx->handle_chunk_is_overflow = true;
}
```

Publishing the new chunk through the old link must follow normal managed-store
ownership rules. A frame link uses stack-root semantics; a link in an existing
`HandleChunk` retains its successor. The API value that caused the transition
was stored before this helper runs, so it remains rooted if allocation reaches a
safepoint.

## Scanning And Updating

Argument slots remain ordinary managed-frame roots. The fixed API-handle chunk
is part of the same managed frame and is scanned as mutable `Value` storage.
Following its final link reaches every overflow chunk, whose layout descriptor
scans and updates all of its slots.

Every safepoint record published while the native call is active must use frame
boundaries that include the complete fixed API-handle chunk, including unused
`not_present` cells and its final overflow-link cell. This remains true during
nested native-to-managed calls: the outer native frame and its entire fixed
handle region stay in the published managed frame chain until the outer call
returns.

The collector therefore reaches all indirect handle slots without scanning the
native C stack or treating `clover_context` as an independent root owner.

The context's `handle_chunk_next` and `handle_chunk_end` are allocation cursors,
not managed roots. They point into the stable Clover stack or stable
`HandleChunk` storage and do not need relocation updates.

## Native Module Initialization

Native module initializers currently receive a `clover_context` from the module
loader rather than through an extension thunk. Before calling the initializer,
the loader reserves a fixed handle region below the current published Clover
stack frontier.

The reservation is an RAII scope:

1. Save the previous Clover stack frontier.
2. Reserve enough lower-addressed stack cells for one fixed handle chunk.
3. Initialize the entire region, including its final link cell, to
   `Value::not_present()`.
4. Publish the region's beginning as the new native stack frontier for both GC
   scanning and nested managed-entry allocation.
5. Initialize `handle_chunk_next` and `handle_chunk_end` from the reserved
   region and call the module initializer.
6. Restore the previous frontier when the initializer returns or unwinds.

Conceptually:

```cpp
CloverStackRootRegion roots(thread, FrameHandleCellCount);
clover_context ctx{
    thread,
    roots.begin(),
    roots.end() - 1,
    false,
};

clover_status status = init_function(&ctx, &builder);
```

The context does not own the region. Moving the frontier downward reserves it so
nested `call_clovervm_function` entry allocates below the handle cells rather
than overwriting them. Restoring the old frontier removes the region as a stack
root; any overflow chain then becomes reclaimable through its frame-region link.

The frontier operation used by this scope must publish both meanings together:
the lowest live stack cell that GC scans while native code is active, and the
boundary below which nested managed entry may allocate. Merely retaining a local
pointer to otherwise unreserved stack cells is insufficient.

## Resolution

In indirect mode, `resolve_handle` interprets the opaque machine word as a
`Value *` and reads that slot. A supported live handle may point to:

- an argument slot in the active managed native frame;
- an occupied slot in the fixed frame API-handle chunk;
- an occupied slot in a live overflow `HandleChunk`.

The final link cell of any chunk, unused capacity, a foreign context, an expired
context, and arbitrary addresses are extension misuse. The public contract does
not require the VM to detect such misuse before dereferencing the handle.

The context deliberately does not retain `fp` or frame-derived validation
metadata. Valid handles may originate in an ordinary thunk frame, a temporary
module-initializer stack-root region, or an overflow chunk. A future debug
validator would therefore need explicit root-region metadata rather than trying
to reconstruct every valid source from one frame pointer; such validation is
not part of this design.

The native callback's returned handle must be resolved before its managed frame
is unwound. An extension may return an incoming argument handle, a fixed-frame
API handle, or an overflow handle.

## Direct Mode

When `native_handle_detail::cl_indirect_handles` is false:

- argument handles contain copies of the argument `Value` bits;
- API-produced handles contain their result `Value` bits;
- `resolve_handle` decodes those bits;
- no API-handle slot or overflow chunk is allocated.

The unused allocation fields may remain in `clover_context`; removing those few
words is not worth introducing a second context layout unless measurements show
a reason.

Both modes must preserve identical pending-exception and API-result behavior.

## Verification

Both flag values must build and run the complete native-module test suite.
Indirect-mode tests should additionally cover:

- argument handles aliasing their original managed argument slots;
- API results using fixed frame storage;
- transition to one and multiple overflow chunks;
- returning argument, fixed-frame, and overflow handles;
- scanning and updating values in every storage class;
- frame-versus-overflow retain and release behavior under deferred refcounting;
- cleanup after normal and exceptional native returns;
- nested native-to-managed calls while the outer handle frame remains live.
