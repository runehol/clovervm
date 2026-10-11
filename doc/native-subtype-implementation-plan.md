# Native Subtype Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial |
| Scope | Common overflow storage, retained native layouts, native subclass validation, and builtin construction |
| Owning layers | Runtime object model owns storage and class validation; memory layer owns sizing and reclamation; builtin families own payload construction; constructor thunks and bytecode own call orchestration |
| Validated against | `c0d9916d` and the separated initializer/constructor stages (2026-10-11) |
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

## Stage 6: Python Exception And StopIteration Initializers

Add Python `__init__` methods to the valid default objects allocated in stages
5b and 5c. Use this family to verify argument-bearing native instantiation and
subclassing through the existing init-only constructor path.

- [ ] Readiness review the Python method installation, variadic argument
  binding, and narrow trusted helpers needed to update native payload fields.
  Keep argument semantics in Python; agree on helper validation, ownership,
  pending-exception contracts, and public descriptor permissions before coding.
- [ ] Implement the Exception-family initializer in Python. Store positional
  arguments as tuple-valued `.args`; cover zero, one, and multiple arguments.
  Resolve keyword-argument behavior in the readiness review.
- [ ] Implement StopIteration's initializer in Python. Store all positional
  arguments in `.args`; set `.value` to the first argument, or None when no
  arguments are supplied. Do not require general call unpacking to delegate.
- [ ] Install the methods so ordinary builtin exceptions and user subclasses
  inherit the appropriate initializer. Preserve separate Exception and
  StopIteration layouts and the allocators from stages 5b and 5c.
- [ ] Test exact builtin instantiation and inherited subclass construction with
  zero, one, and multiple arguments; check class identity, `.args`, `.value`,
  exception formatting, raising/catching, and additional subclass attributes.
- [ ] Test overridden initializers, direct/repeated initializer calls, argument
  errors, initializer errors/results, and inherited method lookup invalidation.
  Keep combined `__new__`/`__init__` construction explicitly unsupported.
- [ ] Follow the builtin-dunder-handler skill for trusted helper contracts and
  lowering. Verify the existing normalized-parameter forwarding handles the
  Python initializer signatures without adding general `funcall(*args, **kwargs)`.
- [ ] Run full debug checks and opcode frame checks if interpreter handlers
  change. Update this checklist and leave changes uncommitted for review.

This stage excludes other builtin families, new allocation opcodes, immutable
constructors, integer boxing, and combined constructor orchestration.

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
