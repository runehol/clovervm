# Native Subtype Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial |
| Scope | Common overflow storage, retained native layouts, native subclass validation, and builtin construction |
| Owning layers | Runtime object model owns storage and class validation; memory layer owns sizing and reclamation; builtin families own payload construction; constructor thunks and bytecode own call orchestration |
| Validated against | Parent `3d6fedad` and the Stage 8c working tree (2026-10-11); full debug checks: 1,577 passed, one skipped |
| Supersedes | N/A |

Implements the accepted [native subtype storage design](native-subtype-storage.md)
and [D-0013](decision-log.md#d-0013-use-common-overflow-storage-for-native-builtin-subclasses).
Complete and verify each stage, then leave its changes uncommitted for review.
Commit only when explicitly requested. Stages 4a, 4b, 4c, and 5 each need
the concrete sketches identified below before their implementation begins.
Family-specific constructors and trusted lowering likewise need sketches when
their contracts are selected in stages 6 through 8.

The pre-stage-1 correction landed in `f13f72cf`: non-slot builtin class and
root-Shape inline capacities are zero, independently of their GC release spans.
The aux-count and instance-size cleanup belongs to the later moving-GC header
refactor; stage 1 preserves current lazy initialization and reclamation behavior.

## Stage 1: Explicit Inline Counts And Allocation Validation

Completed: focused checks passed, followed by 1,515 passing debug tests and
one skipped test. Stage 2 is also complete; subsequent stages remain pending.

Preserve current representations and capacities while making their contracts
explicit. Runtime object types declare their predefined inline region; class
metadata and Shapes describe the allocated capacity.

- [x] Rename `ClassObject::instance_default_inline_slot_count` to
  `instance_inline_slot_count`, with `get_instance_inline_slot_capacity()` as
  the accessor; update parameters and all callers.
- [x] Rename the corresponding Shape accessor to describe inline capacity
  rather than a default; keep the distinction between capacity and occupied
  locations explicit.
- [x] Add `static constexpr uint32_t kPredefinedInlineSlotCount` to every
  concrete `Object` subclass. Declare zero where there are no predefined inline
  fields; do not accidentally inherit an inappropriate count from a base.
- [x] Define the count as the minimum inline extent needed for the predefined
  physical locations, including inherited locations. Computed attributes and
  aliases do not consume additional slots. This count excludes native metadata
  outside the attribute region.
- [x] Use existing predefined indices to establish each count. In particular,
  exceptions require one slot and StopIteration requires two.
- [x] At allocation, validate that physical inline capacity is at least
  `T::kPredefinedInlineSlotCount` and that the initial Shape fits that capacity.
  Keep physical fixed-layout limits separate from the predefined minimum.
- [x] Keep the class allocation count and instance root Shape capacity
  consistent. Preserve the existing initialized-prefix aux count for dynamic
  instances, including its representable bounds; it is not physical capacity.
- [x] Cover bootstrap and cached-Shape allocation paths as well as ordinary
  class-based allocation; validate before using a potentially invalid count to
  initialize storage.
- [x] Verify representative predefined fields, dynamic instance allocation,
  and invalid internal capacity combinations with focused checks.
- [x] Run `ninja -C build-debug all check`.
- [x] Commit the verified stage.

## Stage 2: Common Overflow Storage

The object model owns the common pointer; the memory layer must describe and
release it consistently for every Python-visible heap layout.

- [x] Separate heap-pointer wrappers, pointer-refcount primitives, and shared
  value-tag constants into lightweight headers without an `Object` include
  cycle. Preserve existing wrapper and pointer-refcount semantics; keep
  Value/TValue ownership wrappers separate. This prerequisite is completed
  independently of the storage change below.
- [x] Move the overflow-storage pointer into `Object`, initially null, and
  remove the duplicate field from the existing `SlotObject` base. Use the
  existing `MemberHeapPtr<OverflowSlots>` ownership wrapper.
- [x] Rename the C++ inline-capable base to `InlineSlotObject`. Keep its role
  distinct from native layout identity; existing native IDs remain separate.
- [x] Make overflow reads, writes, growth, and deletion operate through
  `Object`, without requiring a cast to the inline-capable base.
- [x] Retain inline access only for layouts with an actual inline region;
  layouts without that region must have zero Shape inline capacity. Rename the
  native-layout predicate to `native_layout_has_inline_slots` to express that
  capability independently of common overflow storage.
- [x] Update native member offsets, size/release descriptors, static assertions,
  and allocation initialization together. Internal `HeapObject` metadata records
  do not acquire the pointer.
- [x] Ensure release and tracing observe the common pointer exactly once,
  including custom native deallocation paths.
- [x] Preserve attribute permissions and constant-offset cache access; pointer
  availability does not make exact builtins attribute-mutable or broaden their
  existing `__dict__` exposure. Keep that semantic gate separate from overflow
  storage access.
- [x] Verify existing inline and overflow attributes, growth/deletion with
  populated caches, and reclamation of references held in overflow storage.
- [x] Run `ninja -C build-debug all check` and leave the stage uncommitted for
  review.

Verification: 1,518 tests passed, one skipped. The mechanical C++ rename and
the common overflow-storage change were reviewed separately and committed in
that order.

## Stage 3: Retained Native Layout Compatibility Contract

The shared-ID proposal is withdrawn. Keep the existing native layout IDs and
fixed builtin capacities. Define the compatibility contract before wiring MRO
selection into class creation in stage 4.

- [x] Implement the object-model helper
  `Expected<InstanceStorageSelection> select_instance_storage(TValue<Tuple> mro,
  uint32_t ordinary_instance_capacity)`. The result contains the borrowed
  builtin provider (null for all-Instance ancestry), selected native ID, and
  inline capacity. Its input is the candidate class's completed C3 MRO;
  skip entry zero, the class whose storage is being selected.
- [x] Keep `Instance` as ordinary user storage, ignored when a builtin layout
  is selected. Preserve the existing native type checks, dense IDs, indexed
  descriptor tables, and class mappings.
- [x] Scan ancestors from most-derived to least-derived. Select the first
  non-Instance provider and its existing capacity. Accept equal IDs and the
  explicit selected-StopIteration/later-Exception case. If Exception is selected
  and StopIteration appears later, upgrade all three result fields to that
  StopIteration provider. Never downgrade; reject other distinct IDs with
  `TypeError`.
- [x] Drop the separate declared-base compatibility check. Accept the valid
  `(StopIteration, Exception)` declaration; the reverse order is rejected by C3.
  Ordinary mixins and repeated occurrences of one native layout remain valid.
- [x] Preserve the current `__class__` assignment restrictions without enabling
  new builtin reclassification paths. The selector does not mutate classes,
  Shapes, or assignment policy; storage installation remains stage 4 work.
- [x] Preserve predefined indices, fixed capacities, typed exception `.args`,
  StopIteration `.value`, and native allocation/initialization responsibilities.
- [x] Verify the compatibility rules separately from the MRO integration;
  leave aux/header and Instance-size recovery cleanup deferred.
- [x] Run `ninja -C build-debug all check` for code changes and leave the stage
  uncommitted for review.

Verification: seven focused selector tests passed; full debug checks passed
with 1,525 tests passing and one skipped. Class-creation integration remains
stage 4 work. Committed in `acf3bf0c`.

## Stage 4a: Class Storage Selection And Root Shapes

Integrate the stage-3 selector into class creation before installing the final
instance root Shape. Include the allocation guard needed to make this change
safe on its own; builtin constructor support remains in stages 5 and 6.

- [x] Agree on a compact sketch for class construction order, root-Shape
  inheritance, and the unsafe-allocation guard before implementing this slice.
- [x] Compute the candidate class's C3 MRO, call `select_instance_storage`, and
  install the selected native layout and inline capacity before the final
  instance root Shape is installed.
- [x] Build root Shapes with the selected provider's predefined locations and
  existing capacity; select zero without inline storage. Instance-only ancestry
  retains the ordinary-instance capacity policy. Do not add ancestor capacities.
- [x] Preserve native predefined field semantics and the selected storage
  interpretation through Shape transitions and clones, while allowing mutable
  subclass attributes where appropriate.
- [x] Reject conflicting layouts with `TypeError`, including Slice/Exception
  and List/Dict. Support compatible mixins, sibling subclasses, StopIteration
  ancestry, and an Exception-layout subclass before StopIteration in the MRO.
- [x] Guard generic `Instance` allocation as soon as selected native layouts
  become visible: reject constructing classes whose native payload or
  initialization contract that allocator cannot satisfy. Do not defer this
  guard to 4b or enable unfinished builtin constructor paths.
- [x] Verify MRO selection, inherited predefined descriptors, zero/fixed/dynamic
  capacities, class identity, and rejection of unsafe generic construction.
- [x] Run `ninja -C build-debug all check` and leave changes uncommitted for
  review.

Verification: eleven focused selector, root-Shape, native-allocation, and
interpreter tests passed. Full debug checks passed with 1,529 tests passing and
one skipped. The release `check_opcode_frames` target passed.
Stage 4a was committed in `ffee6c1b`. Subclass eligibility and broader allocation
validation are stage 4b; attribute-dictionary exposure remains stage 4c.

## Stage 4b: Subclass Eligibility And Allocation Validation

Validate which builtin classes may be subclassed independently of storage
compatibility, and enforce the selected contract on native allocation paths.
The generic-allocation guard from 4a remains in place.

- [x] Agree on a compact sketch for subclass eligibility and allocator
  validation before implementing this slice.
- [x] Validate builtin subclass eligibility separately from compatible storage;
  reject prohibited builtin bases with `TypeError` during class creation.
- [x] Validate class/root-Shape/native-allocator agreement, predefined minima,
  and physical capacity on ordinary, bootstrap, and cached-Shape paths.
- [x] Preserve native initialization contracts, including tuple-valued exception
  `.args` and initialized StopIteration `.value`; matching slot counts do not
  authorize construction through an incompatible allocator.
- [x] Verify permitted and prohibited bases, allocator mismatch rejection,
  compatible native subclass allocation, and predefined field initialization.
- [x] Replace the stage-1 fixture classes with actual Python class definitions
  selecting matching native storage. Retain explicit native instance allocation
  until builtin constructors support it:
  - `JitConstantFolding.DoesNotUnboxFloatSubclassConstant`
    (`tests/test_jit_constant_folding.cpp`): `FloatSubclass` receives zero inline
    capacity, float ancestry, and distinct class identity.
  - `Interpreter.sequence_search_methods_bypass_subclass_storage_overrides`
    (`tests/test_interpreter.cpp`): `HostileList` and `HostileTuple` receive zero
    inline capacity while preserving their Python-defined overrides.
- [x] Run `ninja -C build-debug all check` and leave changes uncommitted for
  review.

Verification: full debug checks passed with 1,536 tests passing and one skipped.
New coverage includes prohibited bases (including bool), permitted builtin
lineages, native allocator/layout mismatches, cached Slice Shapes, bootstrap
installation, and dynamic Instance sizing. Float/List/Tuple fixtures now use
Python-declared classes. Seven metaclass fixtures were corrected to type ancestry,
ClassObject layout, and its physical capacity. The int class now records BigInt
as its heap storage contract. Committed in `2720047f`.

## Stage 4c: Attribute Mapping Exposure For Native Objects

Expose native object attributes through the existing live mapping, including
exact builtins whose Shapes already declare `__dict__`. Inline-slot capability
does not control namespace exposure; existing Shape permissions control mutation.

- [x] Agree on a compact sketch for class/Shape mapping exposure and mutation
  policy before implementing this slice.
- [x] Refactor `__dict__` exposure and `SlotDict` target validation to support
  native layouts without inline slots, using the common overflow
  pointer. Inline-slot capability must no longer gate mapping exposure.
- [x] Preserve Shape permissions for mapping writes and deletion, including
  immutable/read-only cases; dictionary payload entries remain separate from
  instance attributes.
- [x] Keep the existing `__dict__` descriptors on exact builtins, including
  list, dict, and tuple. Do not introduce an exact/subclass exposure gate or
  change root-Shape construction to hide these mappings.
- [x] Verify mapping reads, writes, deletion, live views, and permission checks
  on native subclasses with zero inline capacity and existing inline layouts.
  Replace exact-container AttributeError expectations with mapping exposure
  and mutation-permission checks; verify dictionary payload isolation.
- [x] Run `ninja -C build-debug all check` and leave changes uncommitted for
  review.

Verification: seven focused mapping tests passed. Full debug checks passed
with 1,537 tests passing and one skipped. Exact list/dict/tuple mappings preserve
existing mutation permissions; native List/Tuple/Dict/Float/String subclasses
and ordinary instances retain live views across attribute additions, deletion,
and re-addition. Dict payload entries remain independent, and exception args
retain their read-only descriptor permissions. Committed in `8262f9e5`.

## Stage 5a: Thunk-Time Allocator Selection

Move allocation selection into thunk generation, initially supporting only the
existing native Instance layout. No new allocation opcode is added here.

- [x] Agree on the selector/emission helper and unsupported-layout error path.
- [x] In `make_init_only_constructor_thunk_code`, inspect the class's selected
  native ID and root Shape before emitting the allocator. Select
  `CreateInstanceKnownClass` for Instance; reject every unsupported layout.
- [x] Preserve the runtime opcode guard and existing capacity validation.
  Do not put a native-layout switch into an opcode handler.
- [x] Preserve new-only construction, normalized initializer argument
  forwarding, the None-result check, and rejection of combined methods.
  Immutable payloads continue to require `__new__`.
- [x] Verify ordinary allocation, unsupported-layout errors, initializer
  arguments/results, and constructor lookup invalidation.
- [x] Run full debug checks; leave this slice uncommitted for review.

Verification: seven focused constructor tests passed. Full debug checks passed
with 1,540 tests passing and one skipped. Unsupported native layouts fail before
argument binding and are not cached as completed thunks. New-only inherited
construction and replacement/deletion invalidation remain supported. The runtime
opcode is unchanged. Stage 5a is complete.

## Stage 5b: Exception Default Allocation

Add one native allocator and wire it into the selector. Builtin initialization
from constructor arguments remains stage 6 work.

- [x] Agree on `CreateExceptionKnownClass`, its class-constant operand, the
  allocation helper, empty args ownership, and opcode frame behavior.
- [x] Allocate Exception native storage using `cls` for subclass identity and
  its root Shape. Validate predefined minima and fixed physical capacity.
- [x] Initialize `.args` to an empty tuple before returning the object. Consume
  no Python constructor arguments; preserve native ownership contracts.
- [x] Select this opcode for Exception in the no-new thunk path. Keep other
  unsupported layouts, including StopIteration until 5c, rejected.
- [x] Verify opcode selection, exact/subclass identity, valid default args,
  user-defined init-only behavior, and failure propagation. Do not add a builtin
  `__init__` or combined new/init machinery in this slice.
- [x] Run full debug checks and release `check_opcode_frames`; leave changes
  uncommitted for review.

Verification: full debug checks passed with 1,543 tests passing and one skipped.
Release `check_opcode_frames` passed. Dedicated-opcode selection, exact and
subclass identity, empty tuple args, overflow attributes, raising allocated
exceptions, initializer argument forwarding, and initializer errors are covered.
The allocation opcode is not included in the hot-path handler list. Committed in `75a73af2`.

## Stage 5c: StopIteration Default Allocation

Extend the same pattern to the distinct StopIteration layout.

- [x] Agree on `CreateStopIterationKnownClass` and its native allocation helper.
- [x] Allocate with the requested class and root Shape, validating the two-slot
  predefined minimum and fixed physical capacity. Preserve StopIteration's
  distinct native layout and ownership contract.
- [x] Initialize `.args` to an empty tuple and `.value` to None before returning.
- [x] Select this opcode for StopIteration in the no-new thunk path, including
  compatible Exception/StopIteration MROs. Consume no constructor arguments.
- [x] Verify opcode selection, exact/subclass identity, both default fields,
  init-only behavior, unsupported layouts, and lookup invalidation.
- [x] Run full debug checks and release `check_opcode_frames`; leave changes
  uncommitted for review. Builtin payload initialization remains stage 6.

Verification: ten focused constructor tests passed. Full debug checks passed
with 1,546 tests passing and one skipped; release `check_opcode_frames` passed.
Coverage includes dedicated opcode selection, exact/subclass identity, empty
args and None value, overflow attributes, both compatible MRO orders, raising
allocated subclasses, initializer arguments/errors, and inherited initializer
replacement/deletion invalidation. Committed in `c0d9916d`.

## Stage 6: Native Exception And StopIteration Initializers

Add C++ `__init__` implementations wrapped as Python `Function` objects to the
valid default objects allocated in stages 5b and 5c. Use this family to verify
argument-bearing native instantiation and subclassing through the existing
init-only constructor path.

- [x] Readiness review agreed on C++ callbacks in `exception_object.cpp`,
  registered with `with_varargs` as `__init__(self, *args)`. The argument binder
  packs positional arguments and rejects keywords; callbacks validate native
  receivers, assign existing `Member` fields, and return None or an exception
  marker. Preserve public descriptor permissions; no new resolver or cache.
- [x] Implement the inherited BaseException initializer in C++. Store the packed
  positional tuple in `.args`, without changing StopIteration `.value` when
  explicitly called on that layout.
- [x] Implement StopIteration's initializer in C++. Store the packed tuple in
  `.args`; set `.value` to the first argument, or None when empty.
- [x] Install the methods so ordinary builtin exceptions and user subclasses
  inherit the appropriate initializer. Preserve separate Exception and
  StopIteration layouts and the allocators from stages 5b and 5c.
- [x] Test exact builtin instantiation and inherited subclass construction with
  zero, one, and multiple arguments; check class identity, `.args`, `.value`,
  exception formatting, raising/catching, and additional subclass attributes.
- [x] Test overridden initializers, direct/repeated initializer calls, argument
  errors, initializer errors/results, and inherited method lookup invalidation.
  Keep combined `__new__`/`__init__` construction explicitly unsupported.
- [x] Follow the builtin-dunder-handler skill for receiver validation and
  registration. Verify normalized-parameter forwarding passes the packed tuple
  directly, without adding general `funcall(*args, **kwargs)` or new opcodes.
- [x] Run full debug checks and opcode frame checks if interpreter handlers
  change. Update this checklist and leave changes uncommitted for review.

This stage excludes other builtin families, new allocation opcodes, immutable
constructors, integer boxing, and combined constructor orchestration.

Validation: `ninja -C build-debug all check` passed with 1,551 tests passing
and one skipped (runtime JIT tiering disabled). Nine focused initializer and
allocator tests passed. No interpreter handlers changed, so opcode frame checks
were not required. Committed in `f57a03d2`.

## Stage 7: Mutable Builtin Allocation And Initialization

Plan and review each eligible family separately, starting with list and dict.
Do not implement this as one general mutable-builtin switch.

- [ ] Give each family a concrete allocation/initializer sketch and test scope.
- [ ] Add a dedicated zero-argument allocator opcode where needed, preserving
  `cls` identity, root Shape capacity, and valid default payload state.
- [ ] Implement payload initialization through the supported init-only path;
  define argument contracts and inherited/overridden method behavior per family.
- [ ] Keep attributes of non-inline layouts in overflow storage. Audit payload
  operations and operation-specific result identity for the family being changed.
- [ ] Verify errors, lookup invalidation, and exact/subclass construction. Run
  debug checks and release opcode frame checks for interpreter changes; leave
  each verified family slice uncommitted until a commit is requested.

### Stage 7a: List

Agreed sketch: `CreateListKnownClass` selects a native empty-list allocator at
thunk creation, preserving the requested class and its zero-inline-capacity root.
The Python initializer has signature `__init__(self, iterable=(), /)`; ordinary
binding handles zero/one positional arguments and keyword rejection without
argument-tuple packing. It clears the native list and appends items using the
existing iteration bytecode and builtin methods. A persistent private
`_clover_list_type = list` bootstrap binding supplies those methods, bypassing
subclass overrides and public `list` rebinding. Replace this binding with closure
capture once closures are supported. Only the temporary initializer function
name is removed after installation.

List count/index/remove and tuple count/index use the same persistent private
binding pattern for their sequence classes, int conversion, isinstance, and
TypeError/ValueError classes. Their dependencies remain stable even when an
equality callback rebinds the public builtin names. Native exception-class
lookup remains a separate follow-up. Regression scripts passed in Clover and local
CPython; the full debug checks passed with 1,559 tests passing and one skipped.

Valid calls clear before obtaining the iterator; iterator errors retain any
partially appended contents. Self initialization clears the list. Existing
init-only thunk invalidation and return checks apply. Custom `__new__` combined
with the inherited initializer remains unsupported under the single-method
constructor boundary. No dict or general native iteration bridge is included.

- [x] Agree on list allocator, initializer signature, and bootstrap dependency.
- [x] Implement allocator opcode and install the Python initializer.
- [x] Verify argument errors, iteration/partial failure, self initialization,
  receiver validation, override bypass, public builtin rebinding, attributes,
  inherited initializer invalidation, and native-operation result identity.
- [x] Run debug checks and release opcode-frame checks; leave uncommitted.

Validation: all 12 focused constructor/initializer checks passed.
`ninja -C build-debug all check` passed with 1,557 tests passing and one
skipped (runtime JIT tiering disabled). Release `check_opcode_frames` passed
without warnings; the allocation opcode tail-calls its allocation continuation
to preserve its frame-free dispatch. Committed in `707b3a97`.

### Stage 7b: Dict New-Only Subclass Construction

Keep dict on the existing new-only thunk path; no initializer or allocation opcode
is added. Retain the native `dict.__new__` function in a private C++ bootstrap binding
before installing its Python replacement, without creating class lookup caches. The native allocator validates a dict-derived
class with Dict storage and zero inline capacity, then creates `Dict(cls)` using
that class's root shape. Exact dicts retain their existing optimized shapes.

The Python `__new__(cls, *args, **kwargs)` allocates through the captured function
and populates through a retained private reference to the shared Python update
implementation. That implementation inserts through builtin dict `__setitem__`,
bypassing subclass `update` and `__setitem__` overrides. Capture its builtin
dependencies privately using the existing bootstrap pattern. A private
`__next__` bridge uses the existing trusted special-method call lowering to avoid
the public next wrapper's mutable dependencies. Preserve the
existing source/keyword argument contract and new-only payload initialization;
combined custom `__new__`/`__init__` remains unsupported. Exact dict calls with
no positional source retain the existing fast path returning the packed keyword
dictionary. Native exception lookup is not part of this slice.

- [x] Agree on the existing native allocator and new-only population boundary.
- [x] Implement subclass-aware allocation and private bootstrap dependencies.
- [x] Verify empty, mapping, pair iterable, and keyword construction, native
  operations and result identity, subclass attributes, override bypass, errors,
  rebinding, and inherited constructor invalidation.
- [x] Run full debug checks and leave committed in `bffa7030`.

Validation: all 111 Dict tests passed during focused verification; four
construction regression scripts also passed against local CPython. Final
`ninja -C build-debug all check` passed with 1,564 tests passing and one skipped
(runtime JIT tiering disabled). No interpreter handlers changed. Changes are
committed in `bffa7030`.

## Stage 8: Immutable Builtin Constructors

Plan and review int, float, str, bytes, and tuple separately. These use the
existing new-only path and must allocate their final native payload directly.

- [ ] Give each family an allocation-correct `__new__`/trusted-helper
  sketch, supported argument contract, and focused test scope before coding.
- [ ] Always box integer subclass results as native heap BigInts, including
  small values; retain SMI normalization for ordinary exact integers.
- [ ] Keep attributes of non-inline layouts in overflow storage. Audit native
  layout/exact-type checks and operation-specific result identity per family.
- [ ] Follow the builtin-dunder-handler skill. Specialize new-only calls by the
  resolved trusted method and applicability, preserving argument validation,
  payload semantics, and lookup invalidation.
- [ ] Verify arguments, inherited/overridden methods, subclass attributes,
  payload operations, and specialized/general path agreement. Run debug checks
  and opcode frame checks for interpreter changes; measure allocation changes
  when needed. Leave each family slice uncommitted until requested.

### Stage 8a: Int Subclass Construction

Agreed sketch: preserve the exact-class first branch in native `int.__new__`,
using the existing new-only thunk and current zero/one int, bool, or string input
contract. Move non-exact targets and formatted errors to a NOINLINE slow helper.
Validate the class, int ancestry, and BigInt layout; reject bool's incompatible
allocator. Match CPython errors for non-types, non-subtypes, and bool.

Add `make_heap_bigint(thread, cls, normalized_view)` in bigint.cpp as an unchecked
storage primitive. It always copies into a heap BigInt using the requested root
shape, including zero and SMI-sized values. Callers establish BigInt layout and
zero inline capacity; parsed heap temporaries have an Owned owner while their
views are consumed across allocation. Additional attributes use overflow storage.

Exact int conversion reuses only exact BigInts. Subclass inputs normalize/copy
to exact integers. Unary plus and nonnegative abs similarly return exact integers;
retain the existing exact-root guard and fast handler for trusted abs. Audit
identity-returning numeric branches and the misleading exact-int helpers used by
bytes without changing their current accepted inputs. No opcode, resolver, cache,
conversion protocol, or combined constructor machinery is introduced.

- [x] Agree on class fast path, allocation primitive, errors, and identity rules.
- [x] Implement subclass construction and exact-result conversion/unary paths.
- [x] Verify values, heap identity, attributes, error text, operations and caches,
  direct/inherited construction, and lookup invalidation.
- [x] Run full debug checks and focused release constructor benchmarks against
  parent commit `bffa7030`; leave uncommitted for review.

Validation: five focused subclass/identity/error checks passed. The main
construction and result-identity scripts also passed against local CPython.
`ninja -C build-debug all check` passed with 1,569 tests passing and one skipped
(runtime JIT tiering disabled). No interpreter handlers changed.

Focused release comparison against `bffa7030` used the same command on each side:

```sh
build-release/benchmark/bench_clovervm --benchmark_filter='BM_IntConstructor(Int|String)' --benchmark_min_time=2s --benchmark_repetitions=5 --benchmark_out=/tmp/clovervm-int-SIDE.json --benchmark_out_format=json
```

| Benchmark | Parent median throughput | Stage 8a median throughput | Change |
|---|---:|---:|---:|
| IntConstructorInt | 101.8M/s | 105.4M/s | +3.5% |
| IntConstructorString | 74.4M/s | 79.0M/s | +6.2% |

Each set's coefficient of variation was below 1%. These focused measurements
show no common-path regression; they do not establish general VM performance.
Raw outputs are `/tmp/clovervm-int-parent.json` and
`/tmp/clovervm-int-after.json`. Committed in `a591ecdf`.

### Stage 8b: Numeric Float Construction

Float previously had no constructor. Stage 8b adds native
`float.__new__(cls, input=0)` using the existing new-only thunk. The first slice
accepts float, int, and bool inputs, including native numeric subclasses and heap
BigInts. String parsing and user conversion protocols remain separate work.
Exact target calls compare class
identity first, reuse only exact Float inputs, and otherwise box an exact Float.

A NOINLINE subclass helper validates class, float ancestry, native Float layout,
and zero inline capacity, then allocates `Float(cls, converted_double)` using
the requested root shape. Convert the numeric payload before allocation; preserve
signed zero, infinity, and NaN. BigInt conversion uses the existing checked
conversion and propagates overflow. Invalid targets get CPython-style errors.

Existing unary plus already copies subclasses and its trusted identity handler
requires the exact root shape; abs/negation/arithmetic already allocate exact
floats. Preserve those contracts and test custom overrides, caches, inherited
construction, attributes, errors, and invalidation. No opcode, cache, initializer,
or combined constructor path is added. Float hashing remains unsupported for
exact and subclass values and is outside this slice.

- [x] Agree on the numeric-only argument and native allocation boundary.
- [x] Implement numeric float construction and regression tests.
- [x] Run full debug checks and focused release constructor measurements.
  Float construction has no working parent baseline; report absolute throughput.
- [x] Leave verified changes uncommitted for review.

Validation: all 13 focused construction/allocator checks passed. Numeric
construction and special-value/result-identity scripts passed against local
CPython. `ninja -C build-debug all check` passed with 1,574 tests passing and one
skipped (runtime JIT tiering disabled). No interpreter handlers changed.

Added release constructor loops for float and integer inputs. Each loop also
compares the converted value and increments an integer accumulator. Five
repetitions at two seconds minimum measured median throughput of 69.9M/s for
float input and 38.7M/s for integer input; coefficient of variation was 1.1% and
1.5%, respectively. There is no working parent float-constructor baseline.

```sh
build-release/benchmark/bench_clovervm --benchmark_filter='BM_FloatConstructor(Float|Int)' --benchmark_min_time=2s --benchmark_repetitions=5 --benchmark_out=/tmp/clovervm-float-constructor.json --benchmark_out_format=json
```

Stage 8b is committed as `3d6fedad`.

### Stage 8c: Str Subclass Construction

Extend the existing `str.__new__(cls, obj="")` on the new-only thunk path.
Preserve the direct exact-str/small-int decimal formatter path and exact-string
input reuse. A NOINLINE subclass helper validates the requested class, str
ancestry, String layout, and zero inline capacity. Convert through the existing
string conversion path, retaining its result with `Owned<TValue<String>>` while
copying the native characters into a String with the requested class root shape.

String subclass inputs must honor overridden `__str__`, unlike the old blanket
String-input identity shortcut. Direct inherited `str.__str__` produces an exact
string from a subclass's underlying text. Custom `__str__` methods may return
string subclasses, and ordinary exact-target construction preserves those
results. Subclass-target construction always copies into the requested class.

Keep existing conversion forms and errors; encoding/decoding arguments remain
unsupported. Audit string operation results, overflow attributes, native hashing,
callback errors and re-entry, invalid allocator classes, and constructor lookup
invalidation. No opcode, cache, initializer, or combined constructor path is added.

- [x] Agree on conversion, identity, native allocation, and hot-path boundaries.
- [x] Implement subclass construction and interpreter regression tests.
- [x] Run full debug checks and compare existing release string constructor loops
  against parent `3d6fedad` with the same command and repeated measurements.
- [x] Leave verified changes uncommitted for review.

Validation: three new interpreter test groups passed. Construction and callback
scripts also passed against local CPython, allowing for the existing non-string
conversion error wording. Full debug checks passed with 1,577 tests passing and
one skipped (runtime JIT tiering disabled). No interpreter handlers changed.

Five-repetition release comparisons measured the following median throughput.
Positive percentages mean higher throughput. Int input was repeated at five
seconds minimum because the initial two-second comparison showed a small 1.6%
decrease; the longer pair instead showed a 1.8% increase. Treat the differences
in both constructor loops as measurement noise, with no speedup claim or
consistent int-constructor regression.

| Constructor loop | Parent | Stage 8c | Change |
|---|---:|---:|---:|
| Int input (5s minimum) | 29.05M/s | 29.59M/s | +1.8% |
| String input (2s minimum) | 49.66M/s | 51.23M/s | +3.2% |

Throughput coefficients of variation were 0.7%/1.0% for the longer int pair and
0.9%/1.1% for the string pair. Raw results are `/tmp/clovervm-str-before.json`,
`/tmp/clovervm-str-after.json`, `/tmp/clovervm-str-int-before-long.json`, and
`/tmp/clovervm-str-int-after-long.json`.

```sh
build-release/benchmark/bench_clovervm --benchmark_filter='BM_StrConstructor(Int|String)' --benchmark_min_time=2s --benchmark_repetitions=5 --benchmark_out=/tmp/clovervm-str-SIDE.json --benchmark_out_format=json
build-release/benchmark/bench_clovervm --benchmark_filter=BM_StrConstructorInt --benchmark_min_time=5s --benchmark_repetitions=5 --benchmark_out=/tmp/clovervm-str-int-SIDE-long.json --benchmark_out_format=json
```

Stage 8c changes are uncommitted for review.

Stages 7 and 8 must define their single-method boundary explicitly. Cases that
require both resolved methods remain unsupported; do not silently skip
initialization or move payload policy between methods to evade that boundary.

## Deferred Work And Completion

- General `funcall(*args, **kwargs)` is a separate prerequisite for combined
  constructor calls. Defer original-call retention, independent new/init binding,
  post-new type checks and actual-class initializer lookup, initializer mutation
  during allocation, and coordinated default new/init argument rules.

- Slack tracking is not implemented by these stages. Future shrinking may
  remove only unused trailing inline capacity common to the entire root Shape
  transition tree. Predefined and descendant-occupied locations set its floor.
- Explicit Python `__slots__` semantics remain deferred as in the design.
- Before each push, run `ninja -C build-release all check` at the exact HEAD
  being pushed.
- [ ] Update the design and this plan's implementation status as stages land;
  mark completed checklist items only after their code and verification land.
