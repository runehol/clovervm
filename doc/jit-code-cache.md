# JIT Code Cache and Publication

| Field | Value |
|---|---|
| Document type | Design |
| Status | Accepted |
| Implementation | Not started |
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

A finalized compiled code unit owns two non-moving slices:

```text
CompiledCodeStorage
    CodeSlice
        writable view
        executable address
        pessimistic capacity
        final encoded size

    ValuePoolSlice
        writable, non-executable address
        naturally aligned Value slots
        slot count
```

The two slices share one compiled-code lifetime but not necessarily one mapping
or adjacent addresses. The code cache must place them within the target's
required PC-relative reach. Neither slice may move while the compiled code can
execute. The moving collector may rewrite pool-slot contents but does not move
the pool slice itself.

The compiled code object exposes the pool base and exact slot count to garbage
collection. Pool storage contains only `Value` slots and is aligned to
`sizeof(Value)`. Multiple compiled functions may own packed slices within the
same writable, non-executable pool pages. A function does not receive a whole
pool page merely to preserve page-level protection.

Initial generated code is immortal. Code-cache retirement, slice reuse, and
dependency-aware reclamation are later policies. Until those exist, a direct
outside-unit transfer may target only code or native entry points whose address
remains stable for at least the complete lifetime of the source code unit.

## Code-cache interface

The emitter writes through a writable view but performs all PC-relative
calculations from the executable address:

```text
CodeSlice
    write_pointer
    execute_address
    capacity
```

Those addresses may be equal, as in the initial implementation and typical
macOS `MAP_JIT` use, or may be aliases in a future platform implementation. The
emitter must never infer executable addresses from writable pointers.

Conceptually, finalization requests storage after its pessimistic sizing pass:

```text
CodeCache::allocate(
    pessimistic_code_size,
    value_pool_slot_count,
    reachability_requirement)
        -> allocation result
```

The result distinguishes successful slices, a near-placement rejection that
requests a far-mode retry, and actual storage-allocation failure. Allocation
failure abandons this JIT compilation and continues execution in the
interpreter; it does not set Python pending-exception state or publish a partial
code object. The attempt's pool-load width is already fixed; allocation success
fixes both addresses before the emitter selects address-dependent forms and
performs final PC-relative encoding. The emitter writes no more than the
requested code capacity or pool slot count. Publication records the final code
size, performs required instruction-cache synchronization, and makes the
executable view callable.

The concrete C++ API may separate reservation, writable scope, publication,
and ownership transfer, but it must preserve this ordering and address split.

## Reachability regions

Code pages and pool pages are allocated within bounded reachability regions.
A region is an address-placement unit, not a function-ownership or page-
protection unit. Many code units and pool slices may belong to one region.

The preferred AArch64 placement keeps a function's code and pool slice within
the signed `LDR`-literal range, approximately 1 MiB. This permits one four-byte
literal load for each `Value` constant. x86-64 RIP-relative loads have an
approximately 2 GiB signed range and therefore fit comfortably in the same
placement scheme.

AArch64 also has the `ADRP` plus `LDR` form with approximately 4 GiB page-
relative reach. The code cache and emitter retain a far-pool path for a code
unit that cannot use near placement. Each AArch64 machine-emission attempt has
an explicit pool mode:

```text
NearLiteral       LDR literal placeholders, 4 bytes per load
FarPageRelative   ADRP + LDR placeholders, 8 bytes per load
```

The JIT driver first emits and sizes in `NearLiteral` mode, then requests an
allocation with the near reachability requirement. If the cache cannot satisfy
that placement, it returns a typed near-placement rejection without allocating
or publishing partial code. The driver discards that emitter attempt and
re-emits once in forced `FarPageRelative` mode. The far attempt is independently
sizeable and testable from its first instruction; there is no partially patched
near-to-far conversion. Failure to allocate the far request abandons this
compilation and retains interpreted execution.

For each accepted allocation the cache returns a reachability guarantee. The
target backend may then choose a fixed-size pool-load policy before ordinary
encoding:

```text
AArch64 near pool:  LDR literal       4 bytes
AArch64 far pool:   ADRP + LDR        8 bytes
x86-64 pool:        RIP-relative MOV  fixed target-specific size
```

Because the chosen policy fixes pool-load size, loads can use inline PC-
relative fixups rather than terminating fragments.

## Inline fixups and fragment layout

The generic emitter distinguishes final-PC rewriting from size-dependent
layout. A fixed-size PC-dependent operation may appear inside a fragment:

```text
CodeFragment
    encoded bytes containing fixed-size placeholders
    inline deferred operations
        fragment-relative byte offset
        target-specific DeferredOperation with min_size == max_size
    optional trailing DeferredOperation whose size may vary
```

The third finalization pass copies ordinary bytes and encodes inline operations
at their actual addresses. An inline operation never changes fragment size. A
variable-size operation remains the sole trailing operation and ends the
fragment for layout purposes.

This uses the same target-specific `DeferredOperation` template parameter for
both roles and does not introduce a separate relocation framework. AArch64
near-pool `LDR` operations and x86-64 RIP-relative pool loads are inline. An
AArch64 far-pool `ADRP` plus `LDR` is also inline when far-pool mode was selected
before emission.

## Tier 1: page-rounded private code

The first executable implementation gives each compiled code unit a private,
page-rounded code mapping:

1. allocate the code pages writable and non-executable;
2. emit and validate the complete code unit;
3. synchronize the target instruction cache as required;
4. change the entire private mapping to read-only and executable;
5. publish the entry point;
6. never reopen that mapping for writing.

One code unit may occupy several pages. A small unit may leave most of its last
page unused. This is accepted for bring-up because it gives a simple one-way
W^X transition and cannot revoke execute permission from code running on
another thread.

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

This tier may suballocate many functions within one code page. It must not use
process-wide page-permission changes to reopen a page that another thread may
be executing. The `CodeSlice` interface remains unchanged; only the cache's
writable-scope and page-allocation implementation changes.

Pool pages remain ordinary writable, non-executable mappings. They do not use
`MAP_JIT` and are not affected by thread-local code-write scopes.

The initial implementation deliberately skips a portable writable-alias tier.
Other platforms may later supply alias mappings or their native JIT APIs behind
the same cache interface.

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
- direct cross-unit targets remain valid for the complete lifetime of every
  source unit that embeds their address.

## Verification

Code-cache tests should cover:

- page-size discovery rather than assuming 4 KiB or 16 KiB pages;
- page-rounded Tier-1 capacity and one-way RW-to-RX publication;
- inability to modify published Tier-1 code through the cache API;
- separate code and pool mappings with code non-writable after publication and
  pool storage writable and non-executable;
- multiple function pool slices packed into one physical pool page;
- stable executable and pool addresses across all three emitter passes;
- `write_pointer` and `execute_address` being treated independently;
- near-region boundary checks for AArch64 literal loads;
- far-region boundary checks for AArch64 `ADRP` plus `LDR`;
- forced near-placement rejection followed by one far-mode emission attempt;
- injected code and pool allocation failures leaving no published entry point,
  no leaked reservation or owned `Value`, and no Python pending exception;
- x86-64 RIP-relative pool reachability;
- inline fixup encoding without fragment creation or size changes;
- final instruction bytes verified through an independent disassembler;
- simulated moving-GC rewrites changing pool slots without changing code bytes;
- publication ordering under concurrent lookup and entry;
- later macOS tests in which one thread writes an unpublished slice while
  another executes published code in the same `MAP_JIT` page.

## Open implementation details

The design does not yet choose:

- the concrete virtual-address reservation and suballocation structures;
- the exact near-region size and code-versus-pool capacity split;
- reclamation and retry strategies for fragmentation and region exhaustion;
- code retirement, dependency tracking, and slice reuse;
- the concrete macOS writable-scope API wrapper and entitlement handling;
- publication and cache-synchronization implementations for each target and
  platform;
- code-cache policies for non-macOS platforms.

Those choices must not leak virtual-memory or platform publication mechanics
into the target assembler or fragment-layout algorithm.

## Related documents

- [JIT Machine-Code Emission](jit-machine-code-emission.md)
- [JIT Compiler and IR](jit-compiler-and-ir.md)
- [JIT Compiler Bring-up Plan](jit-compiler-bring-up-plan.md)
- [Decision Log](decision-log.md)
