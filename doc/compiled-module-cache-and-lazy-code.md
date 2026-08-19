# Compiled Module Cache and Lazy Code Materialization

| Field | Value |
|---|---|
| Document type | Investigation |
| Status | Speculative |
| Implementation | N/A |
| Scope | Persistent compiled-module caching, cache validation, and lazy runtime code materialization |
| Owning layers | Import system owns cache selection and fallback; cache loader owns file validation and decoded records; compiler owns cache production; runtime owns materialized `CodeObject`s and mapping lifetime; interpreter and JIT continue to consume execution bytecode |
| Validated against | `0dd5fa2d` (2026-08-16) |
| Supersedes | N/A |

## Summary

CloverVM should investigate separating its persistent compiled-module cache
format from its in-memory execution bytecode format.

The execution format should remain optimized for interpreter throughput and
cache locality. A persistent format has different priorities: bounded and
cheap validation, mmap-friendly access, lazy materialization, deterministic
production, and controlled evolution across CloverVM versions. Requiring the
two formats to be identical would couple persistence to details such as opcode
encoding, inline-cache layout, and runtime object representation.

The intended pipeline is:

```text
Python source
    |
    | full parse, analysis, and compilation on a cache miss
    v
validated compiled-module cache
    |
    | lower records as required
    v
current CloverVM CodeObject and execution bytecode
    |
    | tier when hot
    v
JIT machine code
```

The cache would be a disposable optimization, not a compatibility ABI or a
security boundary. Any stale or unsupported cache, or any cache that fails the
eager identity, integrity, or envelope checks, must be ignored in favor of
compiling the source normally. A structurally malformed deferred record that
passes those eager checks may instead be discovered after module execution has
begun; that case follows the poisoning and exception policy below.

## Motivation

The current compiler constructs a `CodeObject` for every function while
compiling its parent. The parent stores nested code objects in its constant
table, and `CreateFunction` constructs a `Function` that directly owns a
`CodeObject`. Importing a source module first reads and compiles the complete
source and then executes the module-level code.

That representation is appropriate for execution, but it means a future
persistent cache that merely serializes current `CodeObject`s would inherit
runtime-specific details and would recreate every nested code object on load.
Large applications commonly import modules containing functions that are not
called in a particular process. Eager reconstruction would spend startup time
and heap memory on code that may never execute.

A separate persistent representation could allow module-level code to be made
executable immediately while ordinary function bodies remain in a mapped file
until execution or introspection requires them.

## Goals

- Make a cache hit substantially cheaper than reading, tokenizing, parsing,
  analyzing, and compiling the corresponding source.
- Keep persistent-format validation linear in the amount of data validated,
  with bounded allocations and no runtime pointers.
- Detect accidental cache-file corruption before module execution and fall back
  to source compilation.
- Preserve normal Python module execution and function-definition semantics.
- Avoid lowering persistent records into execution bytecode or constructing
  runtime metadata for unused ordinary function bodies.
- Make no initial promise that unused code pages remain non-resident, because
  the whole-file integrity digest reads them. A future storage-integrity
  mechanism may permit stronger demand paging.
- Allow execution bytecode and inline-cache details to evolve without
  automatically changing the persistent format.
- Treat every cache file as untrusted and make cache rejection safe and cheap.

## Non-Goals

- Define a stable public bytecode format or third-party compiler ABI.
- Preserve cache compatibility indefinitely.
- Store JIT machine code.
- Defer parsing or syntax checking of source when producing a cache.
- Change Python-visible import, function-definition, default-argument,
  decorator, annotation, closure, or class-body behavior.
- Require lazy materialization to be worthwhile for every module or every code
  object.

## Cache Identity and Compatibility

A cache entry must identify more than the source contents. At minimum, cache
selection needs to account for:

- a content hash of the exact source bytes;
- an integrity digest covering the complete cache-file contents;
- persistent cache format version;
- compiler semantic version or compatibility identifier;
- language mode and compilation options that affect semantics;
- platform properties only where the persistent representation depends on
  them.

The source hash detects an accidental mismatch between the source and the
cache's claimed source identity. Because an unkeyed hash can be forged by
someone who can replace the cache, it does not establish provenance or prove
that the cached program was produced from that source. It also does not
establish that the cache is structurally valid or safe to load. A format or
compiler compatibility mismatch is an ordinary cache miss, not an error
reported to Python code. A deployment that treats malicious cache replacement
as in scope needs trusted cache-file permissions or authenticated artifacts in
addition to structural validation.

The cache-file integrity digest has a narrower purpose: it detects accidental
corruption, truncation, and incomplete publication of the exact bytes produced
by the cache writer. The loader must verify it before executing module code and
must treat a mismatch as an ordinary cache miss. The digest may live in a
fixed header field excluded from its own input, or use another unambiguous
canonical encoding selected with the file format.

The digest does not establish provenance, source equivalence, or structural
validity. Someone who can replace the cache can also replace an unkeyed digest,
and a buggy producer can write a structurally invalid file with a matching
digest. Signing or authenticated artifacts are only needed if malicious cache
replacement is in scope. Regardless of integrity checking, every record must
still pass structural validation before it is lowered.

Cache publication should use a temporary file followed by an atomic rename so
readers observe either the previous complete cache or the new complete cache.
The mapping should refer to an immutable file instance for its lifetime; a
path must not be reopened during later materialization and assumed to contain
the bytes that were originally validated. The loader should retain enough file
identity to avoid deleting a different cache that another process has since
published at the same path.

Integrity verification also relies on the mapped file not being modified in
place after its digest is checked. Atomic replacement of the path is harmless
because an existing mapping continues to identify the opened file instance,
but mutation or truncation of that instance would invalidate the earlier
integrity result. The cache writer must never modify a published file in place,
and deployments that cannot enforce that rule need a stronger immutable-file
mechanism or must copy verified bytes into owned storage.

## Persistent Representation

The initial format should be a small, explicitly bounded record format rather
than a serialization of C++ objects. A likely envelope contains:

- magic bytes, format version, byte order, and total file size;
- source identity and compilation compatibility fields;
- a whole-file integrity digest with precisely specified coverage;
- a bounded section directory;
- typed records with explicit sizes;
- string, name, constant, and code-record tables;
- module-level code and nested code records;
- source-position, exception, and function-signature metadata required to
  reconstruct runtime behavior;
- indices or file-relative offsets for all cross-references.

Records must not contain runtime pointers, native function addresses, object
headers, inline-cache contents, JIT state, or ownership state. Integers should
have fixed widths and a specified byte order. Alignment requirements must be
part of the format rather than inferred from host C++ layouts.

The persistent instructions may resemble execution bytecode, but should use
stable semantic operation identifiers and operands that are straightforward to
validate. Lowering may select the current compact opcode spelling, allocate
inline-cache slots, create runtime constants, and derive other execution-only
state.

The format should remain close enough to execution semantics that lowering is
materially cheaper than source compilation. It should not grow into a general
compiler IR with optimization history or an obligation to translate arbitrary
old versions.

## Validation Model

The loader must use checked arithmetic before constructing any runtime object
or exposing a record to the lowering path. Depending on the final format,
validation includes:

- header, section, record, and alignment bounds;
- offset-plus-size overflow;
- known record and semantic instruction identifiers;
- operand counts and operand encodings;
- string, name, constant, metadata, and code-record indices;
- function-signature and frame-layout bounds;
- control-flow targets on instruction boundaries;
- exception ranges and handler targets;
- constant kinds and recursively bounded constant graphs;
- uniqueness, ordering, and non-overlap invariants required by the format;
- all internal cross-references.

Validation APIs must return ordinary failure results and must not rely on
assertions for malformed files. They must not install partially initialized
objects into the module, constant tables, or shared caches. An eager validation
failure discards the mapping and provisional state and performs a normal source
compilation. A deferred validation failure follows the poisoning, deletion, and
exception policy below because module execution has already occurred.

The eager integrity digest detects accidental byte corruption but does not
replace structural validation. Signatures may establish provenance under a
separate deployment policy, but are not required by the initial cache design.

### Validation versus demand paging

There is an unavoidable tension between eager integrity checking and validation
on one side and the strongest mmap benefit on the other. Computing a digest
over the complete cache necessarily reads every cache byte, even if the loader
does not retain or lower the corresponding records. Walking every instruction,
constant, control-flow target, and exception range at import time would add
further work and may allocate validation state for function bodies that are
never used. Conversely, validating only the file envelope cannot make an
unvisited function record safe to lower later.

The proposed policy is to verify the whole-file integrity digest and validate
the envelope, section directory, record index, global resource limits, and the
module-level code needed for import execution eagerly. Each nested code record
remains structurally untrusted and is fully validated immediately before it is
materialized. This avoids eager lowering and runtime-object construction, but
whole-file integrity verification means the initial design does not promise
that pages containing unused function bodies remain non-resident. Every access
path must still distinguish indexed records from structurally validated
records.

The state transition is:

```text
IndexedUntrusted -> Validated -> Materialized
                 `------------> Rejected
```

A rejected record poisons the mapped cache. No other unmaterialized record from
that mapping may subsequently be validated or materialized. Already
materialized `CodeObject`s remain ordinary managed objects and do not read the
rejected mapping again.

This policy needs a precise state machine and fuzz-tested reader before
adoption. In particular, successful envelope validation must not be described
as validating deferred code records. A deferred structural failure after a
matching integrity digest indicates malicious replacement or a cache-producer
defect rather than accidental file corruption. Under the selected threat model,
that case is allowed to break cache transparency and follows the late-failure
policy below.

## Lazy Materialization

On a cache hit, the loader must materialize the module-level `CodeObject`
because importing a module executes its body. Executing a function definition
must still evaluate default expressions, annotations where required by the
selected Python semantics, decorators, and closure creation in normal order.
Class bodies and comprehensions that execute while evaluating module-level code
also require executable code at that point.

An ordinary nested function body can instead be represented by a validated or
indexed persistent code reference:

```text
LazyCodeReference
    mapping owner
    code-record index
    eagerly available function signature and definition metadata
    validation state
    optional materialized CodeObject
```

The mapping owner must keep the mapped bytes alive for as long as any lazy
reference can reach them. A reference must identify a record within that exact
mapping, not a mutable filesystem path. Publication of a materialized code
object must occur exactly once from the perspective of callers; a future
multi-threaded runtime will require synchronization or an equivalent benign
duplicate-and-publish protocol.

First execution follows this path:

```text
Function
    |
    | obtain executable code
    v
validate deferred record if necessary
    |
    | decode constants and lower semantic instructions
    v
runtime CodeObject
    |
    | publish on the lazy reference
    v
interpreter, then existing JIT tiering
```

Materialization can fail because the record is structurally invalid or because
allocation fails. A deferred structural failure discovered after module
execution has begun cannot transparently restart the import from source without
repeating arbitrary Python-visible side effects. CloverVM will not attempt to
repair the record, recompile the source in-process, or restart module execution.

On deferred validation failure, the loader must:

1. mark the mapping and its unmaterialized records as rejected;
2. best-effort delete the cache path, but only if it still names the same file
   instance that was opened and mapped;
3. discard any unpublished provisional validation or materialization state;
4. raise a deterministic Python exception at the operation that forced
   materialization.

Failure to delete the cache must not replace or suppress the validation
exception. The implementation may attach deletion diagnostics to logging or
debug output. The exact Python exception type and message remain to be selected
with the public error-behavior design; an import exception is not necessarily
appropriate when first materialization occurs long after import.

Allocation failure without structural validation failure follows the normal
allocation-error path and must not delete or poison an otherwise valid cache.

### Runtime representation impact

The current `Function` representation requires a `CodeObject` at construction
and copies its call signature from that object. Lazy materialization therefore
cannot be implemented solely inside the import loader. An implementation would
need an agreed representation such as a tagged code provider or a separate
lazy function-code holder, while retaining enough eager metadata for:

- argument acceptance and keyword remapping;
- defaults and docstring behavior;
- closure and defining-module state;
- function and code-object introspection;
- call inline caches and JIT entry selection.

Python-visible access that exposes a code object may force materialization. The
exact introspection surface should be specified when CloverVM implements the
corresponding Python APIs; the cache mechanism must not invent observably lazy
substitutes.

## Memory and Startup Hypothesis

With an mmap-backed cache and per-record materialization, a process pays heap
cost primarily for module-level code and functions it actually executes or
inspects:

```text
mapped module cache
    +-- module code ---------> materialized for import execution
    +-- used function -------> materialized CodeObject
    +-- inspected function --> materialized if required by its API
    +-- unused function -----> remains only in the mapping
```

This can reduce heap allocation and initialization. Lower startup resident
memory is not guaranteed: the whole-file integrity scan reads every cache page,
and source hashing, filesystem I/O, page-cache behavior, mapping metadata, and
retained mappings all have costs. Clean cache pages may later be evicted, so
clustering independently materialized code is still useful for subsequent
locality and residency.

The proposal should advance only if benchmarks on representative import-heavy
applications show meaningful improvements over both source compilation and an
eager persistent-cache loader.

## Relationship to Lazy Parsing

JavaScript engines use lazy parsing partly because source text is often the
only available representation. CloverVM can exploit a different lifecycle:
the first successful compilation completely parses, syntax-checks, analyzes,
and compiles a specific source version, and subsequent processes may reuse its
persistent representation.

This can avoid rediscovering function boundaries or deferring syntax errors on
a cache hit. It does not eliminate the need to validate an untrusted cache, and
it does not justify treating a cache hit as proof that the mapped bytes are
safe.

## Failure and Recovery Rules

- A missing, stale, unsupported, or eagerly rejected cache compiles from source.
- Cache lookup, compatibility, integrity, and eager validation failures are not
  exposed to Python when fallback source compilation succeeds.
- A source compilation failure is reported normally and must not be hidden by a
  stale cache.
- A cache producer writes the integrity digest into a complete temporary file,
  publishes the file atomically, and never mutates it in place.
- The loader places strict configurable limits on file size, section counts,
  record counts, nesting depth, and allocation derived from cached values.
- Eager cache rejection must occur before module execution and must not leave a
  partially published runtime object graph.
- A deferred structural failure after a matching integrity digest is treated as
  malicious input or a cache-producer defect. It poisons the mapping, triggers
  identity-checked best-effort cache deletion, and raises an exception.
- CloverVM does not repair a rejected cache or recompile its source in-process.
- Failure to delete a rejected cache never masks the validation exception.

## Evolution Policy

Cache-format compatibility should be deliberate and bounded. A reader may
support a small set of recent persistent versions when the translation is
simple and tested. It should reject an older version rather than accumulate
compatibility machinery that constrains the compiler or runtime.

Execution bytecode changes do not require a cache-format version change when
the existing semantic records can still lower unambiguously. Persistent
semantic changes, required metadata changes, or validation-rule changes do.
The cache compatibility identifier may also invalidate caches for compiler bug
fixes even when the binary layout is unchanged.

## Suggested Investigation Sequence

1. Measure current import compilation time, code-object allocation, retained
   code memory, and the fraction of imported functions executed.
2. Define a minimal semantic code record, whole-file digest coverage, and
   canonical integrity encoding, then losslessly round-trip a compiled module
   without mmap or laziness.
3. Build a standalone checked reader with adversarial unit tests, mutation
   tests, truncation tests, overflow tests, and coverage-guided fuzzing.
4. Add eager integrity verification and cache loading, then compare them with
   source compilation. This isolates digest, format, and lowering costs from
   lazy-materialization complexity.
5. Specify the runtime code-provider representation, mapping ownership,
   publication and poisoning rules, pending-exception behavior, exception type,
   and introspection forcing points before changing `Function`.
6. Add per-function materialization and benchmark cold startup, warm startup,
   resident memory, page faults, and total work.
7. Confirm or revise the selected validation boundary using security analysis
   and measured page-residency effects.

Each stage should retain source fallback and can be rejected independently.

## Open Questions

- Which semantic instruction representation is sufficiently stable without
  becoming a permanent compiler IR?
- Which constants can remain file-backed, and which must become managed heap
  objects during module materialization?
- Can module-level code refer to nested code by compact record index without
  first creating a heap object for every reference?
- What eager signature, name, closure, source-location, and introspection data
  must a `Function` expose before its code object exists?
- Should a materialized `CodeObject` retain the mapping for file-backed
  constants, or copy everything it needs and release that dependency?
- How are duplicate imports, recursive imports, and future concurrent calls
  synchronized with cache mapping and materialization state?
- Which Python exception type and diagnostic context should a deferred
  validation failure expose?
- Are source-content hashing plus a complete cache integrity scan cheaper than
  compilation for the target workloads, or is a separately specified metadata
  fast path or stronger storage-integrity mechanism needed?
- What cache-size and eviction policy prevents mapped or stored caches from
  becoming an unbounded resource cost?

## Decision Principle

The execution format should be optimized for running code. The persistent
cache format should be optimized for bounded validation, cheap loading, and
selective materialization. They do not need to be the same representation, but
the separation is valuable only if it remains cheaper than source compilation
and does not weaken runtime safety or Python-visible semantics.

Related documents:

- [Import System Design](import-system-design.md)
- [Bytecode Decoding and Block Structure](bytecode-decoding-and-blocks.md)
- [JIT Code Cache](jit-code-cache.md)
