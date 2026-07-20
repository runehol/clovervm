# JIT Code Cache and Publication

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Tier-1 cache and standard mapping backend implemented; emitter and GC publication integration pending |
| Scope | Stable code and constant-pool allocation, PC-relative reachability, writable and executable views, publication, and initial code lifetime |
| Owning layers | The code cache owns virtual-memory placement, page protection, writable and executable views, and storage lifetime; the machine-code emitter owns sizing and encoding against addresses supplied by the cache; the compiled code object owns the resulting code and `Value`-pool slices |
| Validated against | N/A |
| Supersedes | The contiguous code-plus-pool placement in `doc/jit-machine-code-emission.md` |

This document defines how compiled code and its GC-visible `Value` constant
pool receive stable addresses and how completed machine code becomes executable.
It complements [JIT Machine-Code Emission](jit-machine-code-emission.md), which
defines fragment layout and target encoding, and
[JIT Compiler and IR](jit-compiler-and-ir.md), which defines the compiled-code
and garbage-collection contracts.

The initial implementation uses simple page-rounded code allocations. A later
macOS implementation packs functions into `MAP_JIT` pages using thread-local
JIT write protection. Both implementations expose the same code-cache API and
preserve the same executable addresses.

## Storage and ownership model

A finalized compiled code unit identifies two non-moving slices:

```text
CompiledCodeStorage
    CodeSlice
        writable view
        executable address
        committed capacity
        final encoded size

    ValuePoolSlice
        writable, non-executable address
        naturally aligned Value slots
        slot count
```

The two slices need not occupy one mapping or adjacent addresses. The code cache
places them within the numeric span supplied by the target. Neither slice may
move while the compiled code can execute. The moving collector may rewrite
pool-slot contents but does not move the pool slice itself. A unit with no
constants has a `ValuePoolSlice` at the current pool frontier with a slot count
of zero. An existing `JitCodeObject` therefore always has both slices; absence
of compiled code is represented by the containing `CodeObject`'s nullable
`JitCodeObject` reference.

Code and pool storage always occupy different physical pages. A page acquires a
permanent code or pool role before its first suballocation and never changes
roles during the slab's lifetime. This remains true even on platforms whose
code-write mechanism would technically permit mixed permissions through an
alias. Unused space in a pool page may be shared only by other pool slices;
unused space in a code page may never hold GC-rewritten values.

The compiled code object exposes the pool base and exact slot count to garbage
collection. Pool storage contains only `Value` slots and is aligned to
`sizeof(Value)`. Multiple compiled functions may own packed slices within the
same writable, non-executable pool pages. A function does not receive a whole
pool page merely to preserve page-level protection.

Initial generated code is immortal. Each thread has a non-thread-safe
`CodeCache`, but the `VirtualMachine` owns every thread's cache and all of its
slabs until VM destruction, including caches belonging to terminated threads.
Each cache also retains every published `JitCodeObject` until VM destruction.
Code-cache retirement, slice reuse, and dependency-aware reclamation are later
policies. Direct outside-unit transfers and their target pool metadata are
consequently stable until VM destruction.

## Code-cache interface

The emitter writes through a writable view but performs all PC-relative
calculations from the executable address:

```text
CodeSlice
    void *write_pointer
    MachineAddress execute_address
    capacity
```

Those addresses may be equal, as in the initial implementation and typical
macOS `MAP_JIT` use, or may be distinct aliases, as required by the intended
Linux dual-mapping implementation. The emitter must never infer executable
addresses from writable pointers.

`write_pointer` is a `void *`, while `execute_address` and the pool slice's
address are opaque `MachineAddress` values. `MachineAddress` supports checked
offset advancement, checked signed byte and aligned displacement between two
machine addresses, offset within an alignment, and extraction of address bits
solely for materializing an indirect transfer target. The aligned queries
support architectural page-relative encodings without exposing general integer
arithmetic. The type has no implicit pointer or integer conversion. The cache
constructs machine addresses from its mappings; target encoders do not
construct them from writable pointers.

Conceptually, finalization proposes stable placement after its pessimistic
sizing pass:

```text
CodeCache::propose(
    pessimistic_code_size,
    value_pool_slot_count)
        -> proposal result

CodeCache::fits_within_span(
    pessimistic_code_size,
    value_pool_slot_count,
    maximum_span) -> bool
```

The concrete result is an exception-independent `Result<T, Error>` rather than
Python-exception-bearing `Expected<T>`:

```text
enum CodeCacheError
    AllocationFailure
    PublicationFailure

Result<CodeAllocationProposal, CodeCacheError>
```

`Result` follows the existing `Expected` value/error and propagation shape but
carries its error explicitly and never reads or writes pending Python exception
state. `CL_TRY` may be generalized through a `propagate_failure` operation so
both result families retain the same concise call-site behavior.

The target supplies an architectural maximum span; the cache applies its code
granularity, page separation, and pool alignment to decide whether the complete
layout fits. It does not know what instruction or target imposed that bound.
The machine-emission driver calls `fits_within_span` before proposing placement
and owns any near-to-far retry. It must not call `propose` after a failed fit
query. The configured standard slab size must itself satisfy the target's normal
near-placement limit; violating that is a cache configuration error, not a
condition for `propose` to route around. A proposal may allocate a slab so it
can return real stable code and pool addresses, but it does not advance either
allocation frontier and exposes no writable pointers. `CodeAllocationProposal`
directly holds the selected slab and prospective offsets. It is move-only so it
cannot be duplicated, and commit disarms it. Destroying it before commit
requires no action: the cache owns the slab, no frontier has moved, and no
proposed-placement state resides in the cache.

After address-dependent form selection determines the final encoded size,
`CodeAllocationProposal::commit(final_size)` asks its selected slab to advance
both frontiers and return a normal `CodeAllocation` containing the writable code
and pool slices plus the slab offset and exact encoded size needed for
publication. The emitter writes no more than the committed code capacity or
pool slot count. `CodeAllocation` is an ordinary data object with no lifecycle
behavior. After final emission, `CodeCache::publish(allocation)` performs
required instruction-cache synchronization and makes the executable view
callable.

Failure to allocate a slab while proposing placement abandons this JIT
compilation and continues execution in the interpreter; it does not set Python
pending-exception state or publish a partial code object. The attempt's
pool-load width is already fixed, and a successful proposal fixes both addresses
before the emitter selects address-dependent forms and performs final
PC-relative encoding.

Failure of the platform protection transition or another fallible publication
step returns `PublicationFailure`. The committed space remains consumed and
execution continues in the interpreter without setting Python pending-exception
state. This is distinct from failed final instruction revalidation, which is a
hard compiler-invariant failure. Successful publication transfers both slices
into a cache-retained `JitCodeObject`.

## Reachability slabs

The cache normally allocates one-MiB reachability slabs. Code pages grow upward
from the beginning and packed pool slices grow downward from the end:

```text
low address
    code pages
        code units grow upward
    unallocated whole pages
    pool pages
        Value slots grow downward
high address
```

A fit check accounts for page roles: the rounded code-page range may not touch
the page containing the lowest pool slot. A pool page remains RW/NX and packs
slots from multiple functions at `sizeof(Value)` alignment. A code page follows
the platform backend's code-write and publication policy and never contains a
pool slot.

A one-MiB slab keeps every eight-byte pool slot within AArch64's signed
`LDR`-literal range of every instruction in the slab: the greatest positive
displacement to a slot start is at most one MiB minus eight bytes. x86-64 RIP-
relative loads have an approximately two-GiB signed range and therefore fit
comfortably in the same placement scheme.

Placement first tries existing standard slabs. If none fits and the unit's
minimum page-rounded footprint is at most the standard slab size, the cache
allocates a new standard slab. Otherwise it allocates a dedicated, minimally
page-rounded slab for that unit, with code at the beginning and pool pages at
the end. The emitter's preceding `fits_within_span` query establishes that the
unit satisfies its target's numeric bound.

The emitter uses one proposal at a time. Once finalization reports the actual
code size, commit advances the code frontier by only the rounded final size,
immediately recovering pessimistic slack. The returned allocation carries its
publication information; the cache retains no active allocation state.
Destroying a proposal leaves both frontiers unchanged. Once committed, neither
abandonment nor publication failure restores either frontier. Pool addresses do
not move when code slack is recovered.

Proposing placement may allocate a new slab to establish real addresses. An
abandoned standard slab remains available to later proposals. An abandoned
oversized dedicated slab remains mapped and unused until VM destruction; this
bounded loss on a failed compilation path is accepted during bring-up.

AArch64 also has the `ADRP` plus `LDR` form with approximately 4 GiB page-
relative reach. The code cache and emitter retain a far-pool path for a code
unit that cannot use near placement. Each AArch64 machine-emission attempt has
an explicit pool mode:

```text
NearLiteral       LDR literal placeholders, 4 bytes per load
FarPageRelative   ADRP + LDR placeholders, 8 bytes per load
```

The JIT driver retains Core IR, first emits and sizes in `NearLiteral` mode,
then asks whether the rounded code and pool layout fits within the literal-load
span. If it does not, the driver returns a typed near-attempt rejection without
asking the cache to allocate. It discards that emitter attempt and re-emits from
the retained Core IR once in forced `FarPageRelative` mode. The far attempt is
independently sizeable and testable from its first instruction; there is no
partially patched near-to-far conversion. It supplies the wider `ADRP` span to
the same target-independent fit query and allocation call. Failure to allocate
the far request abandons this compilation and retains interpreted execution.

For each accepted allocation the cache returns a reachability guarantee. The
target backend may then choose a fixed-size pool-load policy before ordinary
encoding:

```text
AArch64 near pool:  LDR literal       4 bytes
AArch64 far pool:   ADRP + LDR        8 bytes
x86-64 pool:        RIP-relative MOV  fixed target-specific size
```

Because the chosen policy fixes pool-load size, loads can use per-fragment
relocations rather than terminating fragments.

## Relocations and fragment layout

The generic emitter distinguishes final-PC rewriting from size-dependent
layout. A fixed-size PC-dependent instruction may appear inside a fragment as
encoded template bytes plus a machine-specific relocation:

```text
CodeFragment
    encoded bytes containing fixed-size placeholders
    RelocationEntry<Relocation> records
        fragment-relative byte offset
        target-specific Relocation
    optional trailing DeferredOperation whose size may vary
```

The third finalization pass copies the template bytes and invokes each
relocation at its actual location. A relocation patches only reserved fields
and never changes fragment size. A variable-size operation remains the sole
trailing deferred operation and ends the fragment for layout purposes.

The generic emitter is parameterized independently by the target's
`DeferredOperation` and `Relocation` types. AArch64 near-pool `LDR` instructions
and x86-64 RIP-relative pool loads use relocations. An AArch64 far-pool `ADRP`
plus `LDR` pair uses one relocation when far-pool mode was selected before
emission.

Every final-copy hook receives a writable destination and the independently
computed executable PC. The former is used only to store instruction bytes;
relocation arithmetic, deferred-operation selection, final encoding, and
reachability checks all use the latter. This is required for Linux code caches
that expose separate RW and RX virtual mappings of the same physical pages.

## Platform memory backend

Slab placement is target-independent. A pluggable platform backend owns mapping
creation, writable scopes, instruction-cache synchronization, publication, and
the code-allocation granularity it can safely reuse:

```text
PlatformCodeMemory
    allocate_slab(size) -> Result<PlatformSlab, CodeCacheError>
    enter_writable(slab) -> writable scope
    publish(code range) -> Result<void, CodeCacheError>
    code_allocation_granularity() -> size_t
```

The standard mapping backend reports the host page size. The macOS `MAP_JIT`
backend reports 16 bytes. Other initial backends, including the planned Linux
dual-mapping backend, conservatively report the host page size; a backend may
adopt 16-byte packing later without changing the emitter or cache interface.
Every code-unit start, pessimistic reservation, and committed final size is
rounded up to the backend's reported granularity. Sixteen-byte machine-code
entry alignment is therefore guaranteed under every backend.

## Tier 1: standard `mmap`

The first executable implementation obtains slabs through standard `mmap`.
Each compiled code unit owns a page-rounded range of previously unpublished
code pages within its slab:

1. reserve the code pages writable and non-executable;
2. emit and validate the complete code unit;
3. synchronize the target instruction cache as required;
4. change only that unit's page range to read-only and executable;
5. publish the entry point;
6. never reopen those pages for writing.

One code unit may occupy several pages. A small unit may leave most of its last
page unused. This is accepted for bring-up because it gives a simple one-way
W^X transition and cannot revoke execute permission from code running on
another thread. Finalization may recover pessimistic slack only in whole pages;
the unused tail of a page containing published code cannot be reused. Pool
pages and not-yet-allocated pages in the same slab are unaffected by the code-
page permission transition.

Pool pages are not part of this transition. They remain writable and non-
executable throughout their lifetime and may contain packed slices belonging
to many code units.

Code or pool allocation failure is a recoverable JIT outcome even during
bring-up. The cache releases any unpublished reservation, the compiler releases
its temporary emitter state and owned pool values, no entry point is installed,
and the triggering execution continues in the interpreter. This is distinct
from near-placement rejection, which requests the single far-mode re-emission,
and from malformed encoding or failed final revalidation, which remain compiler
invariant failures.

## Tier 3 on macOS: packed `MAP_JIT` code

The later macOS implementation allocates code pages with `MAP_JIT` and uses the
platform's thread-local JIT write-protection mechanism. The compiling thread
enters a scoped writable state while emitting through the code slice's writable
view and restores executable state before publication. Other threads may
continue executing already published functions in the same packed pages.

This tier suballocates functions at 16-byte granularity within one code page.
It must not use
process-wide page-permission changes to reopen a page that another thread may
be executing. The `CodeSlice` interface remains unchanged; only the cache's
writable-scope and page-allocation implementation changes.

Pool pages remain ordinary writable, non-executable mappings. They do not use
`MAP_JIT` and are not affected by thread-local code-write scopes.

The initial implementation deliberately skips a portable writable-alias tier.
Other platforms may later supply alias mappings or their native JIT APIs behind
the same cache interface.

## `JitCodeObject` ownership

A non-GC `JitCodeObject` records the published code and pool slices as one
metadata unit:

```text
JitCodeObject
    CodeAllocation
    MachineAddress entry
    final code size
    writable Value-pool pointer
    pool MachineAddress
    pool slot count
```

The publishing per-thread cache retains the `JitCodeObject` and its physical
storage until VM destruction. Publication returns a non-owning pointer for
installation into the corresponding `CodeObject`.

`CodeObject` may embed a nullable atomic `JitCodeObject *`. Compilation fully
constructs the object, initializes every pool slot and metadata record, and
publishes the pointer with release ordering. Entrants load it with acquire
ordering. Initial publication is one-time; generated code is not replaced or
retired. `CodeObject::dealloc` does not delete the installed object.

The VM's immortal-code registry traces and rewrites every published object's
recorded pool slots, including code whose original `CodeObject` has become
unreachable but which remains callable through a direct cross-unit transfer.
Until publication, temporary `Owned<Value>` instances in the emitter retain
those values. Successful publication transfers their GC-visible ownership to
the initialized pool; abandoning compilation destroys the temporary owners.

## Publication and concurrency invariants

- executable addresses are final before address-dependent form selection and
  final PC-relative encoding;
- code and pool slices never move while source code may execute;
- no entry point is callable before final encoding and cache synchronization;
- Tier-1 code pages transition from writable to executable exactly once;
- a process-wide permission transition never removes execution from a page that
  another thread may be using;
- a writing thread may not execute JIT code while its platform JIT-write state
  forbids execution;
- pool pages remain writable and non-executable so GC rewriting requires no
  code-page permission transition;
- publication synchronizes all metadata and pool contents before another thread
  can enter the code;
- every generated-code address remains valid until VM destruction;
- a `CodeCache` is used only by its owning thread and requires no allocator
  mutex, while published code may execute on other threads.

## Verification

Code-cache tests should cover:

- page-size discovery rather than assuming 4 KiB or 16 KiB pages;
- page-rounded Tier-1 capacity and one-way RW-to-RX publication;
- one-MiB two-frontier slab placement and exact near-range boundaries;
- permanent separation of code and pool page roles, including their frontier
  collision boundary;
- page-granular standard-backend slack recovery and 16-byte `MAP_JIT` slack
  recovery after pessimistic sizing;
- inability to modify published Tier-1 code through the cache API;
- separate code and pool mappings with code non-writable after publication and
  pool storage writable and non-executable;
- multiple function pool slices packed into one physical pool page;
- stable executable and pool addresses across all three emitter passes;
- `write_pointer` and `execute_address` being treated independently;
- near-region boundary checks for AArch64 literal loads;
- far-region boundary checks for AArch64 `ADRP` plus `LDR`;
- target-owned numeric fit rejection followed by one far-mode emission attempt;
- injected code and pool allocation failures leaving no published entry point
  or owned `Value`, and no Python pending exception;
- injected publication failure consuming the committed storage while retaining
  interpreted execution without a Python pending exception;
- `Result` propagation preserving `CodeCacheError` without touching pending
  Python exception state;
- x86-64 RIP-relative pool reachability;
- relocation patching without fragment creation or size changes;
- deferred-operation and relocation calculations using `execute_address` when
  the writable and executable mappings have different virtual addresses;
- final instruction bytes verified through an independent disassembler;
- simulated moving-GC rewrites changing pool slots without changing code bytes;
- publication ordering under concurrent lookup and entry;
- later macOS tests in which one thread writes an unpublished slice while
  another executes published code in the same `MAP_JIT` page.

## Open implementation details

The design does not yet choose:

- reclamation and reuse strategies for fragmentation and region exhaustion;
- code retirement, dependency tracking, and slice reuse;
- the concrete macOS writable-scope API wrapper and entitlement handling;
- publication and cache-synchronization implementations for each target and
  platform;
- the concrete Linux dual-mapping implementation and policies for other
  non-macOS platforms.

Those choices must not leak virtual-memory or platform publication mechanics
into the target assembler or fragment-layout algorithm.

## Related documents

- [JIT Machine-Code Emission](jit-machine-code-emission.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
- [Decision Log](decision-log.md)
