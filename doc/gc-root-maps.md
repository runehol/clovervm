# GC Root Maps and Writable Unwinding

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started; existing root cells and frame-position publication are groundwork |
| Scope | Interpreter root maps, JIT root maps, and writable register-location recovery; not exit reconstruction |
| Owning layers | Bytecode analysis describes interpreter slots; JIT backend describes physical roots and register saves; runtime walks activations; collector repairs exposed cells |
| Validated against | Root-slot and publication groundwork at `bbd7e999` (2026-11-01); map APIs below are design sketches |
| Supersedes | N/A |

This develops stage 4 of the
[GC implementation plan](generational-copying-gc-implementation-plan.md#4-publish-mutable-roots).
The accepted direction separates three kinds of metadata: interpreter root
maps, JIT root maps, and unwind information. Interpreter exit reconstruction
remains a separate executable transition program. Exact encodings and the
frame-state analysis still require implementation readiness reviews.

## The Contract

At a collection point, every managed reference needed by resumed execution
must have a discoverable writable carrier. Collection repairs that carrier,
and execution subsequently reads or restores the repaired value. If several
physical copies remain authoritative, each must be repaired; object identity
does not justify visiting only one copy.

A root map selects initialized locations containing live values that may be
managed references. Tagged cells proven to contain only inline values, such as
SMIs, may be omitted. Otherwise the visitor inspects the tag: selecting a cell
does not assert that it currently contains a pointer. A boxed float is a
managed reference; an unboxed double is not. Native frame pointers, PCs, raw
scalar words, and uninitialized storage are not tagged reference cells.

The shared scanner interface is the existing `HeapReferenceSlot` and
`StrongSlotVisitor`, not a shared interpreter/JIT map encoding. A slot exposes
the actual writable cell. Collector relocation bypasses ownership operations,
barriers, allocation, and safepoints; it does not perform an ordinary mutator
assignment. Existing slot encoding/storage-class restrictions remain in force.

## Interpreter Root Maps

Each `CodeObject` owns root selections for its supported suspended execution
positions. Positions distinguish the state being described, not merely an
ambiguous instruction address: before/after effects, parked calls, and
exception continuations must have explicit conventions.

Conceptually:

```cpp
struct InterpreterRootMap
{
    std::span<const int32_t> frame_offsets;
};

void visit_interpreter_frame_roots(
    Value *fp, const InterpreterRootMap &map, StrongSlotVisitor visitor)
{
    for(int32_t offset: map.frame_offsets)
    {
        visitor(HeapReferenceSlot(fp + offset));
    }
}
```

Offsets can identify parameters, locals, temporaries, and initialized managed
continuation operands. Native frame metadata is excluded. The published
accumulator and active-code cells are visited separately through the existing
publication machinery; they are not implied by a frame-offset map.

For example:

```text
published position P:
    fp[-3]  parameter a       select: may hold an object
    fp[-2]  local result      select: still observable on exception/recovery
    fp[ 0]  dead temporary    omit: no remaining semantic use
    fp[ 1]  live temporary    select: needed by the suspended operation
    fp[ 2]  unused storage    omit: uninitialized
```

Liveness must include exception, traceback, frame-observation, and interpreter
continuation requirements. Ordinary bytecode-use liveness alone is insufficient.
In particular, do not discard an observable local merely because later normal
instructions do not read it. A boxed temporary consumed by the interpreter
remains required even if optimized code could use an unboxed equivalent.

Every selected cell must be initialized on every path reaching the position.
Where paths merge bound and untouched entry storage, the producer must
establish a valid sentinel through the agreed selective initialization policy,
or supply an explicitly designed alternative. A map cannot safely select a
possibly uninitialized cell, nor omit a possibly live reference. This does not
authorize blanket initialization, post-emission bytecode repair, or runtime
initialization bitmaps. Existing selective prologue clearing remains groundwork,
not proof that exceptional paths are covered.

Suspended callers need maps for their actual parked state. For example, a call
result destination may not yet have been written, while an operand already
consumed by normal bytecode execution may still be needed by a continuation.
The frame walker must recover this position for every caller and native-entry
boundary, not reuse the active frame's resume PC indiscriminately.

### Argument-to-Parameter Handoff

The callee takes over root coverage of parameters. When walking an established
call chain, each activation describes its own incoming parameters, not the
outgoing argument area for the call on which it is currently suspended. The
callee describes that overlapping storage in its post-adaptation parameter
layout. Independently retained caller values remain caller roots; the omission
applies to consumed outgoing argument storage, not all values passed as arguments.

Arguments being accumulated for a different, not-yet-invoked call remain live
temporaries of the current activation. For `f(a(), b())`, while suspended in
`b()`, the saved result of `a()` is still a caller temporary and must be covered
by the caller's map. Only the outgoing argument area handed to the currently
active callee is excluded; merely intending to use a temporary as a future
argument does not transfer its root coverage.

The phase boundary is explicit:

```text
before adaptation:     caller covers outgoing arguments
during adaptation:     adapter covers in-progress state if collection is possible
after call commitment: callee covers initialized parameters
                       parked caller omits consumed outgoing argument storage
```

Do not scan both interpretations of the shared area defensively. Adaptation can
fill defaults, rearrange arguments, and replace extra arguments with containers;
the caller's old outgoing layout is no longer authoritative. Duplicate visits
to a valid cell are not the issue: describing repurposed or uninitialized cells
using an obsolete layout is unsafe.

In the ordinary positional interpreter entry path inspected for this design,
adaptation precedes frame installation and the explicit interpreter call-entry
poll. The JIT-entry branch bypasses that particular poll and needs its own entry
contract. This placement does not establish that adaptation cannot collect:
tuple/dictionary construction and any callback-capable paths require an audit.
Either adaptation is explicitly non-safepointing, or its intermediate references
must be published through a separate adapter state/native root cells. Never
apply the completed callee map to partially adapted storage.

The physical encoding is open: sparse offsets, bitmaps, shared identical maps,
and position ranges are possible. The design does not require one full record
for every bytecode instruction, per-slot identity metadata, or a root map indexed
by source variable name.

## JIT Root Maps

Each `JitCodeObject` owns maps for supported safepoints and suspended call sites.
They describe physical reference carriers after register allocation and
materialization, including generated moves, spills, and fixed-operand copies.
Recovery-only references needed by an exit program also count as live.

Conceptually:

```cpp
class JitRootLocation
{
public:
    static JitRootLocation saved_gpr(uint8_t register_number);
    static JitRootLocation stack_slot(int32_t byte_offset);
};

struct JitRootMap
{
    std::span<const JitRootLocation> locations;
};
```

These are proposed names, not a new use of `TransitionLocation`. Stack offsets
require a specified activation base. A register entry identifies that
activation's logical register contents; unwind information resolves their
current physical storage.

For example:

```text
safepoint S7:
    x0              tagged argument in active saved context   select
    x19             live tagged value                        select
    spill +32       live tagged value                        select
    d0              unboxed double                           omit
    spill +40       unboxed double                           omit
    x21             native frame pointer                     omit
```

A stale spill can be omitted only if execution and recovery will never read it
again. If both a register and a spill remain authoritative, both are roots.
The initial contract uses ordinary managed references, not arbitrary interior
pointers or derived-address fixups. Such addresses must not survive a safepoint
without an explicit later design.

Maps are required at every call site through which collection can occur
transitively, including callbacks and reentry, not just direct collector calls.
They must describe the actual call boundary, including argument setup and
clobbers. Proven no-safepoint calls need no GC suspension map. We do not require
maps at arbitrary machine instructions or enable asynchronous suspension.
Transition execution retains its no-safepoint contract, and this work introduces
no loop-backedge safepoints.

## Unwind Information Is Separate

A JIT root map says which locations hold references. Unwind information says
where an older activation's register contents have been preserved. It is
required even when a saved register contains no reference: the callee need not
know the caller's value type.

The stack walk carries a register-location environment: logical registers map
to writable storage addresses, not merely recovered word values. Starting at
the stopped thread's saved register context, each frame's save/restore rules
resolve the caller's register locations. Registers not displaced retain their
existing carrier; saved registers resolve to the appropriate save cells.
Then the caller's root map selects which resolved locations to visit.

```text
caller A's root map:       x19 is a live root
callee B's unwind rule:   incoming x19 saved at B.sp + 24
collector visits:         writable cell [B.sp + 24]
collector rewrites it:    relocated object reference
B eventually restores:   repaired x19, which A subsequently reads
```

For deeper stacks, save-location rules compose across activations. The rules
must match the actual suspension position, including supported prologue and
epilogue states; assuming that a register was saved somewhere is not enough.
JIT stack roots resolve against their owning activation independently of this
register-location walk.

The safepoint entry wrapper must save active registers needed by the map into
writable cells and restore them from those cells after collection. A caller
cannot retain its only surviving reference in a caller-clobbered register
across a call: allocation must provide a preserved carrier. Call arguments and
native callees have their own rooting/publication obligations. In particular,
native code retaining references across collection must use registered mutable
cells or an explicitly supported save-location contract. Ordinary native
unwinding alone is not a claim of complete native root coverage.

Whether JIT unwind rules use existing platform facilities or compact VM-owned
metadata is an implementation decision still to be reviewed. The essential
requirement is writable-location recovery, not an exception-unwinding API that
returns only register values.

## Scanning and Rewriting

At coordinated collection:

1. Publish the active execution position and writable saved register context.
   Keep activation storage and map/unwind metadata alive and stable.
2. Walk interpreter and JIT activations, including suspended callers and
   native-entry boundaries. Look up each activation's own map and expose its
   selected physical cells as `HeapReferenceSlot`s.
3. Enumerate native handle cells and persistent/published roots through their
   separate existing visitors. Do not treat all frame words or positive
   refcounts as implicit roots.
4. For each cell, decode its reference if any. The collector discovers or
   forwards the object and rewrites the cell to its canonical address.
5. Resume only after repair. Restore registers and reload native handles and
   interpreter state from the repaired cells, never from stale extracted values.

Map enumeration itself performs no managed allocation or mutator reentry.
Collector-only discovery/allocation follows the collector's existing storage
stability and non-reentrancy contract. Root maps do not replace write barriers:
maps identify execution roots at collection points; barriers maintain heap
old-to-young coverage between them.

## Exit Reconstruction Remains a Transition Program

GC does not reconstruct interpreter frames or materialize virtual values.
Unboxed doubles are ignored; virtual values contribute their live managed
components. A boxed representation that already exists and remains needed is
an ordinary managed root.

An exit transition program may copy values into interpreter slots, box doubles,
materialize objects, preserve sharing, and perform recovery fixups. Its logical
frame-slot mapping is not the GC root map. Root liveness and exit reconstruction
may derive from common compiler facts, but neither runtime format subsumes the
other. Unwind information remains separate from both.

## Implementation Boundaries and Verification

This document records direction, not approval to resume all paused frame work.
First settle interpreter position semantics, initialized storage, exceptional
edges, and parked-call requirements. Derive maps read-only and reject unsafe
producer states rather than silently omitting roots. Then assemble the frame
walker. JIT map production and writable unwinding are separate backend slices;
native boundary coverage remains a separate audit.

Tests should repair actual cells and observe resumed execution, not just check
map entry counts. Cover mixed initialized/uninitialized joins, live and dead
temporaries, observable locals, exceptions, suspended calls, and nested native
reentry. For JIT code, cover roots moved/spilled at calls, register-save chains
through multiple callees, authoritative register/spill aliases, and omitted
unboxed values. Verify the safepoint restore uses repaired values. Transition
program regressions must independently preserve exit reconstruction and its
no-safepoint contract.

No map encoding, collector activation, lifetime cutover, or production movement
is implemented or enabled by this design document.
