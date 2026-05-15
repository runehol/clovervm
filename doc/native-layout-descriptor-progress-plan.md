# Native Layout Descriptor Progress Plan

## Goal

Move owned-value release and opaque object-size queries from packed
`HeapLayout` metadata to `NativeLayoutId` keyed descriptors, while preserving
the current reclamation behavior during migration.

The target design is described in
[Native Layout Descriptors](native-layout-descriptors.md). This file tracks the
implementation sequence we intend to check off as it lands. When this plan and
the target design disagree, update the plan; the target design is the source of
truth.

## Key Decisions

- `NativeLayoutId` moves from `Object` to `HeapObject` first, so every heap
  record has one descriptor dispatch key.
- `HeapObject` gets a small auxiliary count field. The first intended use is
  `Instance` physical inline slot allocation count.
- The aux count is physical layout metadata, not semantic attribute metadata.
  `Shape` remains the semantic authority for attributes and storage locations.
- Release descriptors and object-size descriptors stay separate.
- Allocation continues to use concrete type-local size helpers. The
  `object_size_in_bytes(const HeapObject *)` query is for already-allocated
  opaque objects.
- The `SlotObject` split is deferred. Native-layout descriptors must work with
  the current `Object` / `Instance` layout first.

## Stage 1: HeapObject Native Layout Identity

- [x] Add `NativeLayoutId native_layout` to `HeapObject`.
- [x] Add a small auxiliary count field to `HeapObject`, initially intended as
      `uint16_t`.
- [x] Choose field ordering so `HeapObject` stays compact if possible.
- [x] Add `HeapObject::native_layout_id()` and aux count accessors.
- [x] Remove `Object::native_layout` as a separate field.
- [x] Move `Object::native_layout_id()` to `HeapObject` rather than leaving a
      delegating `Object` wrapper.
- [x] Update native-layout checks at object call sites to use the inherited
      `HeapObject::native_layout_id()` accessor.
- [x] Keep `HeapLayout layout` during the transition.

Deliverable: every heap record can carry a native layout identity, while release
still behaves as it does today.

## Stage 2: Construction Plumbing

- [x] Thread native layout ID initialization through static object
      construction.
- [x] Thread native layout ID initialization through dynamic object
      construction.
- [x] Initialize the aux count to zero for all objects at first.
- [x] Keep object constructors and allocation helpers using existing
      `HeapLayout` metadata for size and value-span data.
- [x] Add assertions that constructed heap objects do not retain
      `NativeLayoutId::Invalid`.

Deliverable: all allocated heap objects have a valid native layout ID, with no
descriptor-driven release yet.

## Stage 3: Descriptor Facade With Explicit Legacy Entries

- [x] Add release descriptor types:
      `StaticSpan`, `DynamicSmiSpan`, `DynamicAuxSpan`, `Custom`, and
      transitional `LegacyHeapLayout`.
- [x] Add object-size descriptor types separately from release descriptors.
- [x] Add a reclamation-facing owned-value release facade.
- [x] Route reclamation through the facade instead of open-coding
      `HeapLayout` decoding at the call site.
- [x] Represent not-yet-migrated layouts with explicit `LegacyHeapLayout`
      descriptor entries, not an implicit missing-entry fallback.
- [x] Make debug/startup validation require every native layout ID to have a
      descriptor entry, including explicit `LegacyHeapLayout` entries.
- [x] Keep remaining `LegacyHeapLayout` entries explicit in the descriptor
      table so migration progress is visible in one place.

Deliverable: reclamation has one descriptor-shaped entry point, even though most
unmigrated layouts may still read the old packed metadata through explicit
legacy descriptor entries.

## Stage 4: Static Native Layout Descriptors

Migrate fixed layouts first, in small groups.

- [ ] Add class-local value-span declarations for easy static layouts.
- [ ] Add namespace-scope native-layout trait registrations for those layouts.
- [ ] Validate descriptor offset/count parity against current `HeapLayout`
      metadata.
- [ ] Migrate `List`.
- [ ] Migrate `Dict`.
- [ ] Migrate `Function`.
- [ ] Migrate `RangeIterator`.
- [ ] Migrate `TupleIterator`.
- [ ] Migrate `ListIterator`.
- [ ] Migrate `Exception`.
- [ ] Migrate `StopIteration`.
- [ ] Migrate `String` after confirming its static release span and dynamic
      size query are represented cleanly.
- [ ] Migrate `ClassObject` as a static release span while the `SlotObject`
      refactor remains deferred.

Deliverable: the common fixed object layouts release through descriptor table
entries, with parity tests protecting the migration.

## Stage 5: Tuple DynamicSmiSpan

- [ ] Add `DynamicSmiSpan` release support.
- [ ] Use `Tuple::size_value` as the dynamic count source.
- [ ] Start the tuple release span at `size_value`.
- [ ] Set tuple `additional_value_count` to include fixed owned cells in the
      same contiguous span, including the count cell itself.
- [ ] Add tuple descriptor parity tests for compact dynamic layouts.
- [ ] Add tuple descriptor parity tests for expanded dynamic layouts.
- [ ] Add or keep reclamation tests proving tuple elements are cleared before
      child values are released.

Deliverable: the dynamic-SMI release path is proven before instance slot counts
move to the aux field.

## Stage 6: Instance DynamicAuxSpan

- [ ] Add `DynamicAuxSpan` release support.
- [ ] Store each instance's physical inline slot allocation count in
      `HeapObject` aux count during dynamic construction.
- [ ] Keep the aux count equal to the physical allocation count, not
      `Shape::property_count()` or any other semantic descriptor count.
- [ ] Use the class-declared first owned value offset as the instance release
      span start.
- [ ] Set instance `additional_value_count` to the fixed owned fields before the
      inline slot payload in the current C++ layout.
- [ ] Replace instance release dependence on `HeapLayout` value count with the
      descriptor's aux-count path.
- [ ] Add tests that instances with different class default inline slot counts
      release exactly their allocated slot payload.
- [ ] Add tests that later shape transitions do not change the aux count.

Deliverable: instance release and future opaque instance sizing are local to the
object header, while `Shape` remains responsible for semantic storage.

## Stage 7: Opaque Object Size Query

- [ ] Add `object_size_in_bytes(const HeapObject *)`.
- [ ] Add static object-size descriptors for fixed-size layouts.
- [ ] Add dynamic object-size descriptors for tuple and instance.
- [ ] Derive tuple opaque size from tuple length.
- [ ] Derive instance opaque size from `HeapObject` aux count.
- [ ] Keep allocation on concrete `T::object_size_in_bytes(args...)` or existing
      type-local helpers rather than dispatching by native layout ID.
- [ ] Use the opaque query only for validation, accounting, debugging, or other
      already-allocated-object paths.

Deliverable: size queries no longer need to decode packed `HeapLayout`, but
allocation remains type-directed.

## Stage 8: Custom Release Layouts

- [ ] Add custom release descriptor support.
- [ ] Migrate `CodeObject` to custom release.
- [ ] Ensure custom release clears owned cells before releasing copied values
      where applicable.
- [ ] Keep custom release entries cold and explicit.
- [ ] Do not use custom callbacks for layouts that fit the static or dynamic
      span forms.

Deliverable: layouts that cannot honestly be represented as contiguous value
spans have explicit custom teardown.

## Stage 9: Internal Heap Records

- [ ] Give internal heap records native layout IDs after `NativeLayoutId` lives
      on `HeapObject`.
- [ ] Migrate `ValidityCell`.
- [ ] Migrate `Scope`.
- [ ] Migrate `Shape`, likely with custom release because of transition vector
      ownership.
- [ ] Migrate `OverflowSlots`.
- [ ] Erase template backing records into concrete native-layout types where
      needed, such as `ValueArrayBacking` and `RawArrayBacking`.
- [ ] Store normalized dynamic value-cell counts in backing records, not element
      counts that require release-loop scaling.
- [ ] Decide and document the `HeapPtrArrayBacking` release representation.

Deliverable: every heap record can be discovered and released by native layout
ID, not just Python-visible objects.

## Stage 10: Remove Legacy Release Dependence

- [ ] Verify every live heap object kind has a descriptor entry.
- [ ] Verify there are no remaining `LegacyHeapLayout` release descriptors.
- [ ] Remove the `LegacyHeapLayout` release kind from reclamation release.
- [ ] Remove packed value-count and value-offset dependence from reclamation.
- [ ] Keep any remaining size metadata only where still needed by allocation or
      transitional validation.
- [ ] Update reclamation and heap tests to assert descriptor-driven release.

Deliverable: owned-value release is fully descriptor-driven.

## Later: SlotObject Split

- [ ] Introduce `SlotObject` only after descriptor migration is stable.
- [ ] Move physical overflow-slot storage mechanics from `Object` to
      `SlotObject`.
- [ ] Move inline-slot storage mechanics from `Object` to `SlotObject`.
- [ ] Make `Instance` a dynamic-size `SlotObject`.
- [ ] Consider making `ClassObject` a fixed-size `SlotObject`.
- [ ] Preserve the invariant that inline or overflow `StorageLocation` implies
      the storage owner is physically a `SlotObject`.

This refactor is intentionally not on the critical path for native-layout
descriptors. The class-local descriptor offset declarations should absorb the
physical layout move when it happens.
