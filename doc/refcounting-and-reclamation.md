# Refcounting and Reclamation

| Field | Value |
|---|---|
| Document type | Architecture contract |
| Status | Accepted |
| Implementation | Implemented |
| Scope | Deferred reference counting, zero-count processing, roots, safepoints, and slab release |
| Owning layers | Runtime ownership, thread state, managed frames, allocator, and reclamation |
| Validated against | `3f5f608` (2026-05-30) |
| Supersedes | N/A |

We want to have a GIL-less implementation. However, we still want Python C extension compatibility, so we need to do something here.

We prefer to do deferred reference counting. This means that we don't refcount values on the stack, only on the heap. Loading values from the heap to a local variable, as well as moving values between local variables and temporaries is therefore a matter of a `MOV`, whereas we only need to adjust refcounts when storing to a heap, such as when assigning to a dictionary, object, list or similar. When we do this, we `INCREF` the object we're storing, and we `DECREF` the object we're overwriting, if any.

This is safe, because there's no way for the heap to point at the stack.

We'll also have non-reclaimable immortal/interned values. These are marked in
the value so you don't have to dereference them, and the refcount itself will be
`-1`. The current single-threaded implementation keeps refcounts as plain fields
behind helper APIs. The no-GIL version should make `INCREF` and `DECREF` atomic
for reclaimable heap references.

Reclaimability is determined by allocation heap and pointer tag, not by the
semantic type of the object. Objects allocated from a `ThreadState`'s
thread-local refcounted heap are reclaimable and use the refcounted pointer tag,
currently `value_refcounted_ptr_tag == 0x10`. VM startup objects, code objects,
modules, and other long-lived runtime objects may still be reclaimable if they
are allocated from a thread-local refcounted heap.

The global immortal heap is not reclaimable. Objects allocated there use the
non-reclaimable pointer storage class, currently represented by
`value_interned_ptr_tag == 0x08`, have `refcount == -1`, never enter the ZCT, and
are skipped by the reclaimable-object lifecycle. The current
`value_interned_ptr_tag` couples two concepts: interned value semantics, where
pointer equality implies value equality, and immortal lifetime, where the
allocation is never reclaimed. The implementation may keep them tied together
for now, but the design should name the concepts separately so later storage
classes do not inherit the wrong semantic assumption.

Whenever we `DECREF` an object and it reaches zero, we do not delete it immediately, simply put it on a zero-count table to delete later. One array per thread to avoid threading issues. When we run out of slab memory or the zero count table overflows, it's time to free unused memory. At that point, we bring the threads to a safe point, where the managed Clover stacks are no longer being changed, and we look at the zero count tables. Objects still referenced from managed stacks stay in the zero count table for the next safepoint. Objects with no heap refcount and no stack reference are deleted, which may `DECREF` members of these objects. Note that children themselves can be pointed-to from the managed stacks.

Maybe we build a bloom filter from the stacks to achieve this quickly. No, this doesn't work, we cannot forget to decref some objects due to lossiness, because these objects will never show up again on the ZCT and therefore leak. They can also hold on to large graphs of stuff transitively.

This scheme would naturally place all newly allocated objects on the zero count tables, which is wasteful. Maybe we can have a scheme where bump-allocated objects from slabs don't go on the list, but we have a way to traverse objects on bump-allocated slabs and note what slabs we've allocated from.

## Zero Count Table Semantics

The zero-count table is not a list of objects known to be dead. It is a list of
objects whose heap reference count is zero and whose final liveness has to be
rechecked at a safepoint.

An object that is in the ZCT and is also found in a managed stack root is still a
zero-refcount object. It is only temporarily protected by the current stack
state, so it must remain in the ZCT and be tested again at the next safepoint.
Removing it from the ZCT would be incorrect: when the stack reference later
disappears, no heap `DECREF` will necessarily occur to re-add it.

ZCT processing should therefore compact surviving candidates in place:

```c++
size_t scan = 0;
size_t keep = 0;

while(scan < zct.size()) {
    HeapObject *obj = zct[scan++];

    if(obj->refcount > 0) {
        obj->lifecycle_state = HeapLifecycleState::Normal;
        continue;
    }

    if(stack_roots.contains(obj)) {
        zct[keep++] = obj;
        continue;
    }

    obj->lifecycle_state = HeapLifecycleState::Reclaiming;
    reclaim(obj);  // May append newly-zero children to zct.
}

zct.resize(keep);
```

The scan bound is dynamic (`scan < zct.size()`) because reclaiming one object may
`DECREF` its children and append new zero-count candidates. Those candidates can
be handled during the same safepoint.

The ZCT should be represented as a contiguous array such as
`std::vector<HeapObject *>`. The scan uses integer indices, not iterators,
because appending may reallocate the underlying storage.

Newly allocated reclaimable objects start with heap `refcount == 0`. If such an
object is only ever stored in managed frame slots and is later forgotten, no
heap `DECREF` will occur to enqueue it. Therefore a valid zero-refcount object
must be discoverable by reclamation.

The implementation discovers those young objects through slab metadata rather
than eager allocation-time ZCT enqueue. As described in
[Heap Slab Allocation and Reuse](heap-slab-allocation-and-reuse.md), thread-local
heaps remember slabs that have been active since the previous reclamation, and
reclamation scans those slabs' valid-object bitmaps to discover young
`refcount == 0 && lifecycle_state == Normal` candidates. The ZCT primarily
tracks older zero-refcount objects whose stack-root protection survived a
previous reclamation, plus objects that reached zero through heap `DECREF`.

Young bitmap-discovered candidates use the same stack-root filter as ZCT
entries. If a young zero-refcount object is rooted, it transitions
`Normal -> InZct` and is appended to a ZCT so it remains discoverable after the
current stack root disappears. If it is not rooted, it transitions directly to
`Reclaiming` and uses the normal reclamation teardown path.

## Heap Object Lifecycle State

The runtime should prevent duplicate ZCT entries with an explicit heap object
lifecycle state. This is a correctness invariant, not merely an optimization:
duplicate ZCT entries can lead to double reclamation.

The current implementation stores this state directly in `HeapObject`, next to
the refcount and native layout metadata.

```c++
enum class HeapLifecycleState : uint8_t {
    Normal,
    InZct,
    Reclaiming,
    Dead,
};
```

The intended transitions are:

- `Normal -> InZct`: a `DECREF` reduces the heap refcount to zero and enqueues
  exactly one ZCT entry.
- `InZct -> Normal`: a safepoint observes that the object's heap refcount became
  positive again.
- `InZct -> InZct`: a safepoint finds the zero-refcount object in managed roots,
  so it is compacted into the retained ZCT prefix.
- `InZct -> Reclaiming`: a safepoint finds no heap refcount and no managed root.
- `Reclaiming -> Dead`: teardown has completed and the allocation is no longer
  a valid heap object.

`Reclaiming` and `Dead` objects must never be enqueued. Objects with the
non-reclaimable pointer storage class do not participate in this
reclaimable-object lifecycle.

Any allocation entry point that creates reclaimable objects must take or recover
the owning `ThreadState` reclamation context so zero-count children discovered
during later teardown have a well-defined ZCT destination. Newly allocated
objects are not eagerly enqueued in the ZCT; valid-object bitmap discovery finds
young zero-refcount objects that never received a heap `DECREF`. Allocation
entry points for the global immortal heap do not participate in this
reclaimable-object lifecycle.

For the current single-threaded implementation, refcount and lifecycle state may
be plain fields. The GIL-less design should treat them as one logical atomic
state, because a transition such as "refcount reached zero and lifecycle was
`Normal`" must enqueue at most one ZCT entry. This suggests eventually packing
the refcount and lifecycle state into the same 64-bit word, so future atomic
compare/exchange operations can update and validate them together.

The transitions are driven by two places:

- `DECREF` only performs `Normal -> InZct`. If a `DECREF` reaches zero and the
  object is already `InZct`, it must not enqueue a duplicate entry.
- Safepoint ZCT processing performs the `InZct` exits. A positive heap refcount
  means the object has been retained by heap ownership and can return to
  `Normal`. A zero heap refcount plus a stack root keeps the object in `InZct`.
  A zero heap refcount with no stack root moves the object to `Reclaiming`.

`Reclaiming` is useful even if memory is released immediately after teardown. It
states that the object is currently being destroyed, so child `DECREF`s,
reentrant checks, or accidental duplicate ZCT entries cannot treat it as an
ordinary zero-count object. `Dead` states that the allocation no longer contains
a valid heap object; it is available for slab accounting and eventual reuse.

This explicit lifecycle state is also a useful foundation for later cycle
collection. A trial-deletion or trial-`DECREF` collector will need to distinguish
ordinary objects, zero-count candidates, objects being investigated as part of a
candidate cycle, and objects being destroyed. The initial state machine does not
reclaim cycles, but it keeps room for those future states without overloading
the numeric refcount itself.

Known cycle to resolve: `ClassObject` and `Shape` already form a strong
metadata cycle. A class object owns its own shape and instance-root shape, while
those shapes store `class_value` so normal class discovery can go through
`Shape::get_class()`. That `class_value` cannot simply be weak without replacing
the class-discovery invariant. Cycle collection, immortal bootstrap ownership,
or a class/shape-specific ownership rule must eventually break this cycle. The
current young-object bitmap discovery keeps newly allocated zero-refcount
objects visible without allocation-time ZCT enqueue, but it does not solve
cycles.

## Stack Root Collection

Safepoint validation should first build a temporary set of stack roots, then use
that set as a filter while processing the ZCT.

The order is deliberate: scan stacks first and build one root set, then scan the
ZCTs afterward. Do not build a ZCT-candidate set first and use it to filter stack
scanning; the stack scanner should remain a simple conservative producer of
possible heap roots.

The initial implementation should conservatively scan the managed stack slice
published by safepoint arrival:

1. Read from `lowest_live_stack_slot` upward to the permanent Clover frame
   sentinel stored in `ThreadState`.
2. Include frame headers and any other slots in that published range.
3. For each slot, if it has refcounted-pointer shape, insert the
   `HeapObject *` into the root set.
4. If `accumulator_or_not_present` is a refcounted heap pointer, insert it too.

This set is only a safepoint-local protection filter. It does not increment
refcounts and it does not remove objects from the ZCT. If the scanner finds a
stale pointer-shaped value that is not an object currently present in the ZCT, it
must not dereference that value and it has no reclamation effect.

The stack scanner must not dereference candidate pointers or consult allocator
metadata to validate them. Conservative scans may encounter stale values that
point to memory that was formerly an object. The scanner should insert
refcounted-pointer-shaped candidates into the root set and let ZCT processing
decide whether a candidate matters.

Frame pointer and PC header fields may have pointer-shaped bits, but they point
into the Clover stack or code storage rather than to heap object starts. They
may consume root-set space if scanned conservatively, but they should not cause
false positive ZCT hits.

The scanner should not infer liveness from frame shape, bytecode, or
`CodeObject` metadata. When the interpreter arrives at a safepoint, the
committed call/return slow path writes a normalized stack-scan record into
`ThreadState`. The explicit safepoint bytecodes were removed; safepoint polling
now lives in places where the interpreter has already committed to a stable
frame state. There are two cold helper paths: one publishes the accumulator as
live, and the other publishes `Value::not_present()`. The record contains:

- the lowest live stack slot for that safepoint;
- `accumulator_or_not_present`.

The stack scanner consumes this record directly: it scans from
`lowest_live_stack_slot` upward to the permanent Clover frame sentinel, and it
adds `accumulator_or_not_present` if that value is a refcounted heap pointer. The
accumulator is not saved into the frame merely for safepointing; if it is live,
safepoint arrival publishes it in `accumulator_or_not_present`. Otherwise that
field is `Value::not_present()`.

An empty managed stack does not need a separate scan-record shape; it is
represented by `lowest_live_stack_slot` equal to the `ThreadState` sentinel.
Debug builds should validate that `lowest_live_stack_slot` is within the managed
stack bounds and does not scan past the sentinel.

The initial scanner does not need to walk frame headers as a linked frame chain.
If a future scanner needs frame-structured traversal, the safepoint slow path
can also checkpoint the current `fp`, but that is not part of the
first root-scanning contract. One likely reason to add this later is support for
a mixed native/managed stack model where the scanner must recognize transition
frames between native and managed execution.

Temporary call argument spans are tricky because runtime call adaptation can
place values on the stack that were not statically pushed by codegen. Examples include
tuples or dictionaries materialized for `*args` or `**kwargs` adaptation for the
concrete callee. Codegen may know the lowest argument slot it emitted, but it
does not necessarily know the full runtime extent created by adaptation.

The initial interpreter reclamation design should therefore only allow
safepoints at places where call argument preparation is known dead: function
entry, normal function return to the caller, and loop back branches. Exception
propagation and unwind paths are not safepoint locations. At those points, the
temporary call argument span does not need to be scanned as call-entry state.

These safepoint locations also give useful liveness facts:

- At function entry, the callee frame has been established and argument
  adaptation has completed. The current implementation publishes the
  frame-owned scan slice computed from the `CodeObject`'s below-frame slot
  count, and uses the no-accumulator helper path.
- At normal function return, the safepoint occurs while the returning frame is
  committed: the caller frame has been restored, and the return value lives in
  the accumulator carried by the published interpreter state. The implementation
  again publishes the restored current frame's conservative scan slice and uses
  the accumulator-live helper path. This keeps return safepoints centralized
  next to `Return`-style opcodes instead of requiring every opcode that may
  indirectly call Python code, such as arithmetic or descriptor operations, to
  identify and safepoint all possible return PCs.
- At codegen-marked loop back edges, expression temporaries are dead. Locals and
  frame headers remain live. Loop safepoints are intentionally deferred; when
  they are added, codegen should mark loop back edges and publish the
  appropriate lowest live local/header boundary through the no-accumulator
  helper path.

Only normal function-return safepoints normally publish the accumulator as live.
Function-entry and future loop-back safepoints normally publish
`Value::not_present()` for `accumulator_or_not_present`; the committed helper
selection makes accumulator liveness explicit rather than something the scanner
guesses.

The first implementation may still conservatively scan broader frame-owned
storage if that is simpler, but these liveness facts define where eager clearing
or narrower interpreter scans are allowed without per-PC safepoint maps.

Do not try to support safepoints during call preparation or argument adaptation
by reconstructing a dynamic call-argument extent. That path is too subtle to
make into a reliable root-scanning contract.

Native helpers must not reach a safepoint while holding borrowed `Value`s only
on the C++ stack. Before any safepoint check, borrowed values that must remain
live must be discharged onto the Clover managed stack or otherwise owned by a
heap/native owner.

Function entry means after the callee frame is fully established and after
argument adaptation has completed. It does not mean the caller's function-call
site. This is deliberately different from the more conventional "call
safepoint" location, because call-site and adaptation state may contain transient
temporary call argument layouts that are not part of the initial root-scanning
contract.

Safepoints around native/managed transitions need a separate design pass.

For the first version, a conservative root set such as
`absl::flat_hash_set<HeapObject *>` is acceptable. Stale values in dead
temporaries or dead call argument slots may retain zero-refcount objects for extra
safepoints, but they do not compromise memory safety.

Stack hygiene can reduce conservative-scan retention. Frame slots may be
initialized to non-pointer sentinels, and codegen may clear known-dead
temporaries or call argument spans around safepoint-capable operations. This
clearing is root-set hygiene only; stack slots do not own references and must
not `DECREF` when cleared. It is not a correctness requirement: conservative
scans may find stale pointer-shaped values in frame slots, and those values are
harmless as long as the scanner treats roots only as a filter against ZCT
entries and never dereferences non-ZCT candidates.

If stale frame values become a practical retention problem, prefer eager
clearing of dead frame slots over per-PC frame maps. Per-PC safepoint maps remain
possible for future JIT or optimization work, but they are not part of the first
interpreter reclamation design.

## Safepoint Requests

Allocation pressure, ZCT growth, or explicit runtime requests do not force an
immediate stop at arbitrary program points. They set a pending safepoint request.
Attached threads continue until they reach one of the allowed safepoint checks,
publish their stack-scan record, and park or participate in reclamation there.
Multiple requests while a safepoint is already pending or in progress coalesce
into that safepoint.

This keeps allocation helpers and call-adaptation helpers from needing to make
their transient state scan-safe. Reclamation only runs after all attached threads
have arrived at allowed safepoints.

While a safepoint request is merely pending, attached threads may continue
executing, allocating, and mutating their stacks and ZCTs. Once a thread reaches
the safepoint slow path, publishes its scan record, and enters `GC`, it must stop
allocation and stack/ZCT mutation until the coordinator releases it.

The safepoint fast path is a hot-path poll embedded in committed call/return
paths: check the global pending-safepoint flag and continue immediately if it is
clear. This fast path must not set up a stack frame. When the flag is set, the
opcode handler tail-calls a cold helper that publishes the stack-scan record and
enters coordination/reclamation.

The first implementation can be single-threaded, but the API shape should leave
room for a global coordinator that iterates the registered `ThreadState`s tracked
by `VirtualMachine`.

Future multi-threading should use a
[CPython/PEP-703-style thread status model](https://peps.python.org/pep-0703/#thread-states):

- `Attached`: the thread may touch Python/Clover objects and mutate managed
  state. Attached threads must poll safepoints.
- `Detached`: the thread promises not to touch Python/Clover objects. It does
  not block safepoint coordination, but its published stack-scan record still
  participates in root scanning. Detached threads' stacks and ZCTs are scan-safe
  precisely because detached threads do not mutate them.
- `GC`: the thread is stopped or otherwise enrolled in the current safepoint
  reclamation cycle. Its scan record is stable.

Every registered thread has a stack-scan record. "No managed Clover stack" is
represented by a stack slice whose lower bound is the sentinel, not as a separate
state:

```text
lowest_live_stack_slot == stack_sentinel
accumulator_or_not_present == Value::not_present()
```

A future native-extension API may support a CPython-like detach operation for
long-running native code. Detaching must leave behind a stable stack-scan record.
Native code may be running without an OS thread available to execute VM code on
request, so safepoint reclamation cannot rely on asking detached native code to
publish new state. Native transition handling will likely be responsible for
publishing the detached thread's stable scan record, but that boundary needs a
separate design pass.

Thread exit and unregistering also need an ownership boundary. A thread must not
drop a non-empty ZCT when it exits. Its ZCT entries must be transferred to a
parent thread's ZCT, such as the main thread or the thread performing the join.
The exact parent-selection policy can be decided later, but ZCT handoff is
required before the exiting `ThreadState` disappears.

## Safepoint And Reclamation Phases

The first implementation can run these phases with a single `ThreadState`, but
the protocol should map cleanly to multiple threads.

### 1. Request

Some thread or allocator slow path sets a global pending-safepoint flag. The
request may be caused by ZCT growth, allocation pressure, tests, or a future
policy such as bytes allocated since the last safepoint.

In a multi-threaded runtime, setting the flag is global. It does not stop threads
immediately and it does not require the requesting thread to be at a scan-safe
point.

### 2. Arrival

Attached threads poll the global flag at allowed committed interpreter states:
function entry and normal function return today, and codegen-marked loop back
edges later. If the flag is clear, the hot path continues. If the flag is set,
the slow path
publishes the thread's stack-scan record:

- `lowest_live_stack_slot`;
- `accumulator_or_not_present`;
- any future fields needed by the scanner.

The multi-threaded design has the thread enter `GC` state and stop mutating
managed stacks, ZCTs, and heap ownership until reclamation is complete. The
current single-threaded implementation instead runs the per-arrival testing
callback with the active thread still installed, then enters
`ThreadState::NoActiveThreadScope` and calls
`VirtualMachine::complete_safepoint()`.

Detached threads do not need to execute an arrival slow path, but they must have
left behind a stable scan record at detach time. If the detached thread has no
managed Clover stack, that record publishes `lowest_live_stack_slot` equal to
that thread's sentinel.

### 3. Quiescence

The coordinator waits until every registered thread is quiescent:

- attached threads have reached an allowed safepoint, published their scan
  record, and entered `GC`;
- detached threads already have stable scan records and do not need to run VM
  code to participate.

Only after this point may reclamation inspect all thread stack records and all
ZCTs.

Because all attached threads are in `GC` and detached threads promise not to
mutate VM state, the coordinator does not need per-ZCT locks while processing
ZCTs during the safepoint.

### 4. Root Collection

The coordinator builds one temporary root set by scanning every registered
thread's published stack slice and any live accumulator value in that thread's
scan record. The scanner performs only tag checks for refcounted-pointer shape
and inserts matching pointer values into an `absl::flat_hash_set<HeapObject *>`.

Root collection can be parallelized independently of VM thread count. Once
quiescence is reached, stack records are immutable scan tasks. There may be more
stack records than worker threads; workers can scan tasks from a queue, build
worker-local `absl::flat_hash_set<HeapObject *>` instances, and merge those sets
afterward. Detached threads are just scan records in this model; they do not
need to be available as workers.

### 5. Serial ZCT Processing

After root collection, the coordinator processes every registered thread's ZCT
serially. There is no common ZCT in the first design. Each thread ZCT is a vector
processed with the dynamic `scan < zct.size()` loop and scan/keep compaction.
Objects with positive refcount transition back to `Normal`. Zero-refcount objects
found in the global root set remain `InZct` and are compacted into that ZCT's
kept prefix. Zero-refcount objects not found in roots transition to
`Reclaiming`, are torn down, and eventually transition to `Dead`.

The coordinator also walks the valid-object bitmaps for slabs that were active
in each `ThreadLocalHeap` since the previous reclamation. For each young object
with `refcount == 0 && lifecycle_state == Normal`, the same root set acts as a
filter:

- if the root set contains the object, transition `Normal -> InZct` and append
  it to a ZCT so it remains discoverable after the current stack root
  disappears;
- otherwise transition it to `Reclaiming` and run the ordinary reclamation
  teardown path.

ZCT entries and slab-discovered young objects are two candidate sources feeding
one reclamation mechanism. They must not become separate collectors with
different liveness rules.

While a ZCT is being processed, ordinary static and dynamic span teardown uses
explicit reclamation scanning code rather than ordinary hot-path `decref()`
calls. The current implementation looks up the object's `NativeLayoutId`,
processes its release descriptor, clears each owned cell, and releases the
copied child value through the reclamation context. Child references that reach
zero append to the same ZCT currently being walked. This improves locality and,
more importantly, lets the dynamic scan process cascaded children in the same
safepoint sweep. If a cascaded child is stack-rooted, it is copied into the kept
prefix and remains in that ZCT for the next safepoint.

Layouts with `CustomDealloc` descriptors are cold paths. Reclamation installs
the reclaimed thread as active before invoking the custom deallocator, so that
deallocator may use ordinary `decref()` and still route cascaded zero-count
children to the correct thread's ZCT.

ZCT processing should be serial in the first multi-thread-capable design. Unlike
root scanning, it mutates lifecycle state, compacts ZCTs, runs teardown, appends
cascaded zero-count children, clears slab valid-object bits, and records slabs
for release checks. These operations are too subtle to parallelize before the
basic invariants are proven.

A single heap object must appear in at most one ZCT across the whole VM. This is
enforced by the object lifecycle state: only the transition `Normal -> InZct`
enqueues, and future multi-threaded implementations must make that transition
globally unique with atomic state. ZCT membership is queue placement, not object
ownership; it is fine for a cascaded child to be retained in a different thread's
ZCT than the one that allocated or last touched it, because reclamation appends
cascades to the ZCT currently being walked.

Debug builds should validate the global uniqueness invariant at safepoints by
checking all ZCT entries with a temporary `absl::flat_hash_set<HeapObject *>`.
This is not the mechanism that prevents duplicates; it is a guardrail for finding
lifecycle bugs.

### 6. Slab Accounting And Reuse

When object teardown is complete, the object's valid bit is cleared in the
owning slab's valid-object bitmap, and that slab is remembered for a later
release check. Slab lookup uses the global slab lookup granule map described in
[Heap Slab Allocation and Reuse](heap-slab-allocation-and-reuse.md). Slabs are
released only at explicit batch points after candidate processing and epoch-list
scanning are complete.

Teardown may process owned child fields word-by-word. For each owned field, it
should copy the child value, clear the field's ownership in the object, and then
release the copied child through the reclamation path that appends newly-zero
children to the currently walked ZCT. Two different fields may legitimately own
the same child and therefore release it twice; the lifecycle state still prevents
duplicate ZCT enqueue.

### 7. Release

After all ZCTs have been processed and slab accounting is complete, the
coordinator clears the global pending-safepoint flag and releases arrived
threads. Threads that were attached before the pause resume from their safepoint
poll sites. Detached threads remain detached and must reattach before touching
Python/Clover objects again.

clovervm will only implement the limited API.

The native machine stack is not part of the managed stack-scanning contract.
The interpreter and native C++ functions run on the native stack; future JIT code
may run on the Clover stack for managed execution, but must switch to the native
stack before calling C++/native code. Any live `Value` that must survive such a
transition has to be present in a managed frame slot, retained by a heap object,
or owned by native code through ordinary refcounting. We should not rely on
finding roots by treating arbitrary native stack words as managed `Value` slots.

Native stack values follow the CPython C API ownership model: native code that
keeps a heap object live across safepoint-capable work must own a reference.
Those references are heap refcounts, not stack-scan roots.

Reclamation must not execute arbitrary Python finalizers. Initial teardown is
VM-native only: clear owned child references, `DECREF` children, and run only
destroy hooks that do not invoke Python code or allow Python-level resurrection.
Weakrefs, `__del__`, and C-extension `tp_dealloc` resurrection semantics require
a separate design.

Worker-thread participation in parallel root scanning, out-of-memory behavior
while a safepoint is pending, and the eventual packed atomic refcount/lifecycle
header layout are later design topics.

The accumulator is represented in safepoint records as
`accumulator_or_not_present`. Initial interpreter safepoints publish it only at
normal function return, where it carries the return value. Function-entry and
loop-back safepoints publish `Value::not_present()`. Future JIT exits or
native/managed transitions must follow the same rule: any live
accumulator/register value that is not present in the managed stack slice must be
published explicitly in the transition's scan record or owned by native code
through refcounting.
