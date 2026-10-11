# Native Subtype Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial |
| Scope | Common overflow storage, retained native layouts, native subclass validation, and builtin construction |
| Owning layers | Runtime object model owns storage and class validation; memory layer owns sizing and reclamation; builtin families own payload construction; constructor thunks and bytecode own call orchestration |
| Validated against | `ffee6c1b` plus the stage 4b implementation (2026-10-11) |
| Supersedes | N/A |

Implements the accepted [native subtype storage design](native-subtype-storage.md)
and [D-0013](decision-log.md#d-0013-use-common-overflow-storage-for-native-builtin-subclasses).
Complete and verify each stage, then leave its changes uncommitted for review.
Commit only when explicitly requested. Stages 4a, 4b, 4c, and 5 each need
the concrete sketches identified below before their implementation begins.
Family-specific constructors and trusted lowering likewise need sketches when
their contracts are selected in stage 6.

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
as its heap storage contract. Changes remain uncommitted for review.

## Stage 4c: Attribute Mapping Exposure For Native Subclasses

Expose overflow-backed native subclass attributes through the existing live
mapping without conflating inline-slot capability with namespace exposure.

- [ ] Agree on a compact sketch for class/Shape mapping exposure and mutation
  policy before implementing this slice.
- [ ] Refactor `__dict__` exposure and `SlotDict` target validation to support
  native subclass layouts without inline slots, using the common overflow
  pointer. Inline-slot capability must no longer gate mapping exposure.
- [ ] Preserve Shape permissions for mapping writes and deletion, including
  immutable/read-only cases; dictionary payload entries remain separate from
  instance attributes.
- [ ] Keep exact-builtin mapping exposure deferred: whether exact `dict` and
  `list` expose a read-only mapping is a separate decision, not implied by 4c.
- [ ] Verify mapping reads, writes, deletion, live views, and permission checks
  on native subclasses with zero inline capacity and existing inline layouts.
- [ ] Run `ninja -C build-debug all check` and leave changes uncommitted for
  review.

## Stage 5: Complete Constructor Protocol

Constructor orchestration preserves the original call across allocation and
initialization. Builtin payload policy stays outside the thunk builder.

- [ ] Agree on a compact sketch for the original-argument representation, thunk
  signature, and forwarding mechanism. Keep argument values live and rooted
  across allocation without an unconditional tuple/dict wrapper.
- [ ] Call resolved `__new__` with the requested class and original arguments;
  bind allocation and initialization arguments independently.
- [ ] Test the general allocation result with
  `is_instance_of_known_class`, retaining its exact-class fast path and MRO
  fallback without adding another type-test cache.
- [ ] Return unrelated allocation results directly, without resolving,
  validating, or calling initialization.
- [ ] After allocation, resolve `__init__` using the returned object's actual
  class and MRO. Preserve descriptor binding and ignore instance attribute
  hooks; observe initializer replacement during `__new__`.
- [ ] Require the initializer result to be `None` and propagate lookup, binding,
  allocation, and initialization exceptions explicitly.
- [ ] Preserve coordinated default `object.__new__`/`object.__init__` argument
  rules and existing constructor lookup invalidation. Invalidation affects
  subsequent calls without cancelling an executing thunk.
- [ ] Verify independent defaults and keyword binding, unrelated and subclass
  results, initializer mutation during allocation, descriptor binding,
  non-`None` results, and replacement/deletion of inherited constructor methods.
- [ ] Measure ordinary exact-class construction in a release build and check
  that the call representation does not force variadic collection allocation.
- [ ] Run `ninja -C build-debug all check` and leave the stage uncommitted for
  review.

## Stage 6: Builtin Allocation And Trusted Specialization

Implement and verify the general family-owned constructor behavior before
specializing it. Split this stage into reviewable changes by builtin family
and lowering; commit them only when explicitly requested.

- [ ] Start with allocation-correct exception constructors using their distinct
  native layouts, allocating with the requested class's root Shape and
  predefined minimum.
- [ ] Supply or inherit allocation-correct `__new__` methods for the remaining
  builtin families. Non-instantiable classes reject construction instead of
  falling through to generic slot-only allocation.
- [ ] Preserve native payloads and subclass identity for variable-sized
  builtins; their additional attributes remain overflow-only.
- [ ] Always allocate integer subclass results as native heap BigInts, even for
  small values. Retain SMI normalization for ordinary exact integers.
- [ ] Audit native-layout versus exact-type checks and operation-specific result
  identity, including reuse/conversion paths that must return exact builtins.
- [ ] For each family, agree on the allocator applicability and argument/payload
  contract before implementing it, following the builtin dunder-handler skill.
- [ ] Add allocator-specific trusted semantics and equivalent specialized
  lowering selected by the resolved method, not by class layout or method name.
- [ ] Omit the post-allocation type check only when the trusted contract proves
  the result has the requested class; retain actual-class initialization.
- [ ] Guard specialization with existing lookup invalidation and retain the
  general call path for unsupported contracts.
- [ ] Verify each family through Python-visible construction, arguments,
  overrides, subclass attributes, payload operations, and exception behavior;
  verify specialized and general paths agree.
- [ ] Measure known builtin allocation and ordinary construction in release
  builds. Run `ninja -C build-debug all check` for each completed code slice.
- [ ] Leave verified family implementations and specializations uncommitted
  for review; commit them separately when explicitly requested.

## Deferred Work And Completion

- Slack tracking is not implemented by these stages. Future shrinking may
  remove only unused trailing inline capacity common to the entire root Shape
  transition tree. Predefined and descendant-occupied locations set its floor.
- Explicit Python `__slots__` semantics remain deferred as in the design.
- Before each push, run `ninja -C build-release all check` at the exact HEAD
  being pushed.
- [ ] Update the design and this plan's implementation status as stages land;
  mark completed checklist items only after their code and verification land.
