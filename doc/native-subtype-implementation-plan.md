# Native Subtype Implementation Plan

| Field | Value |
|---|---|
| Document type | Implementation plan |
| Status | Accepted |
| Implementation | Partial |
| Scope | Common overflow storage, shared pure-slot layout, native subclass validation, and builtin construction |
| Owning layers | Runtime object model owns storage and class validation; memory layer owns sizing and reclamation; builtin families own payload construction; constructor thunks and bytecode own call orchestration |
| Validated against | `f13f72cf` plus stage 1 changes (2026-10-11) |
| Supersedes | N/A |

Implements the accepted [native subtype storage design](native-subtype-storage.md)
and [D-0013](decision-log.md#d-0013-use-common-overflow-storage-for-native-builtin-subclasses).
Complete and verify each stage, then leave its changes uncommitted for review.
Commit only when explicitly requested. Stages 4 and 5 need
the concrete sketches identified below before their implementation begins.
Family-specific constructors and trusted lowering likewise need sketches when
their contracts are selected in stage 6.

The pre-stage-1 correction landed in `f13f72cf`: non-slot builtin class and
root-Shape inline capacities are zero, independently of their GC release spans.
The aux-count and instance-size cleanup belongs to the later moving-GC header
refactor; stage 1 preserves current lazy initialization and reclamation behavior.

## Stage 1: Explicit Inline Counts And Allocation Validation

Completed: focused checks passed, followed by 1,515 passing debug tests and
one skipped test. Later stages remain unimplemented.

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
- [ ] Move the overflow-storage pointer into `Object`, initially null, and
  remove the duplicate field from the existing `SlotObject` base. Use the
  existing `MemberHeapPtr<OverflowSlots>` ownership wrapper.
- [ ] Rename the C++ inline-capable base to `InlineSlotObject`. Keep its role
  distinct from the shared `NativeLayoutId::SlotObject` introduced in stage 3.
- [ ] Make overflow reads, writes, growth, and deletion operate through
  `Object`, without requiring a cast to the inline-capable base.
- [ ] Retain inline access only for layouts with an actual inline region;
  layouts without that region must have zero Shape inline capacity. Rename the
  native-layout predicate to `native_layout_has_inline_slots` to express that
  capability independently of common overflow storage.
- [ ] Update native member offsets, size/release descriptors, static assertions,
  and allocation initialization together. Internal `HeapObject` metadata records
  do not acquire the pointer.
- [ ] Ensure release and tracing observe the common pointer exactly once,
  including custom native deallocation paths.
- [ ] Preserve attribute permissions and constant-offset cache access; pointer
  availability does not make exact builtins attribute-mutable or broaden their
  existing `__dict__` exposure. Keep that semantic gate separate from overflow
  storage access.
- [ ] Verify existing inline and overflow attributes, growth/deletion with
  populated caches, and reclamation of references held in overflow storage.
- [ ] Run `ninja -C build-debug all check` and leave the stage uncommitted for
  review.

## Stage 3: Shared Native Layout For Pure-Slot Objects

Replace the six slot-only physical layouts with one allocation and tracing
contract. Semantic class identity remains in the Shape.

- [ ] Introduce `NativeLayoutId::SlotObject` for `Instance`,
  `ModuleLoaderObject`, `ModuleSpecObject`, `Slice`, `ExceptionObject`, and
  `StopIterationObject`; remove their superseded IDs and update every in-repo
  consumer.
- [ ] Allocate a contiguous inline `Value` region after the common object
  prefix. Record its actual physical count in the object header.
- [ ] Use that object-local count for size recovery and release/tracing, without
  depending on class or Shape metadata for allocation extent.
- [ ] Enforce the predefined minimum from stage 1. Preserve existing field
  indices and each builtin's initialization semantics, including exception
  `.args` and StopIteration `.value`.
- [ ] Allow spare inline capacity above the predefined minimum; additional
  attributes use inline storage until full, then overflow storage.
- [ ] Update native helpers, typed conversions, trusted handlers, and builtin
  registration so a shared layout ID alone never proves a particular builtin
  semantic type. Preserve the typed tuple contract of exception `.args`.
- [ ] Retain separate IDs and fixed physical inline capacities for `Function`,
  `ClassObject`, and `ModuleObject`, including their native metadata.
- [ ] Verify semantic type discrimination between objects with the shared ID,
  predefined field access, different allocation counts, cached attribute access,
  and reclamation of both inline and overflow references.
- [ ] Run `ninja -C build-debug all check` and leave the stage uncommitted for
  review.

## Stage 4: Class-Creation Storage Validation

The runtime class-creation path selects compatible instance storage before
installing the instance root Shape. Allocation enforces that selected contract.

- [ ] Agree on a compact metadata/API sketch for native payload ancestry,
  predefined-slot compatibility, and capacity selection.
- [ ] Walk the class MRO using each base's instance-storage requirements;
  ordinary Python mixins must not impose generic slot-only storage on a native
  family.
- [ ] Validate subclass eligibility separately from storage compatibility.
- [ ] Reject incompatible native payloads or predefined-slot requirements with
  `TypeError` during class creation. Equal shared native IDs alone do not prove
  compatibility.
- [ ] Build shared-slot root Shapes with inherited predefined locations and
  sufficient capacity; preserve fixed capacities for native metadata layouts
  and select zero for layouts without inline attribute storage.
- [ ] Preserve the selected storage interpretation in Shape transitions and
  clones. Validate allocator/native-layout agreement.
- [ ] Preserve native predefined field semantics while allowing mutable
  subclass attributes where appropriate.
- [ ] Refactor `__dict__` exposure and `SlotDict` target validation to support
  object layouts without inline slots. Such objects use the common overflow
  pointer for their attribute mapping; inline-slot capability must no longer
  gate that mapping's exposure. Apply the selected class/Shape exposure and
  mutation policy, including tests for native subclasses with zero inline
  capacity. Whether exact builtins such as `dict` and `list` also expose a
  read-only mapping remains deferred; stage 2 preserves current exposure
  behavior.
- [ ] Verify compatible mixins and sibling subclasses, conflicting bases,
  predefined-slot compatibility, zero/fixed/dynamic capacities, and class
  identity. Do not enable construction through a generic allocator that cannot
  satisfy the selected native storage contract.
- [ ] Revisit the stage-1 fixtures that explicitly prepare zero-capacity native
  subclass classes. Use actual Python class definitions once class creation
  selects matching native storage, retaining explicit native instance allocation
  until builtin constructors support it:
  - `JitConstantFolding.DoesNotUnboxFloatSubclassConstant`
    (`tests/test_jit_constant_folding.cpp`): `FloatSubclass` must receive zero
    inline capacity while retaining float ancestry and distinct class identity.
  - `Interpreter.sequence_search_methods_bypass_subclass_storage_overrides`
    (`tests/test_interpreter.cpp`): `HostileList` and `HostileTuple` must receive
    zero inline capacity while preserving their Python-defined overrides.
- [ ] Run `ninja -C build-debug all check` and leave the stage uncommitted for
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

- [ ] Start with allocation-correct exception/shared-slot constructors,
  allocating with the requested class's root Shape and predefined minimum.
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
