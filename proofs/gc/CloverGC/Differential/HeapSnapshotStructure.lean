import CloverGC.Differential.HeapWireCaptureValidity
import CloverGC.Differential.HeapCompletedFormatCompleteness

namespace CloverGC.Differential

theorem OriginalStateProjection.is_original (projected : OriginalStateProjection before header after) :
    Format.isDestination after = false := by cases projected <;> rfl

theorem OriginalStateProjection.is_young (projected : OriginalStateProjection before header after) :
    after.isYoung = before.isYoung := by cases projected <;> rfl

theorem OriginalProjection.is_original (projected : OriginalProjection heap state units collection before after) :
    Format.isDestination after.state = false := by
  obtain ⟨_, _, shape, _, _⟩ := projected.object
  exact shape.is_original

theorem OriginalProjection.is_young (projected : OriginalProjection heap state units collection before after) :
    after.state.isYoung = before.state.isYoung := by
  obtain ⟨_, _, shape, _, _⟩ := projected.object
  exact shape.is_young

theorem CopyProjection.object_id {heap : PreparedHeap n k} {state : Copying.EvacuationState n} (projected : CopyProjection heap state units pair output) :
    output.objectId = (heap.originals.get pair.1.origin).objectId := by
  have same := congrArg Prod.fst projected.identity
  exact same

theorem CopyProjection.allocation_id {heap : PreparedHeap n k} {state : Copying.EvacuationState n} (projected : CopyProjection heap state units pair output) :
    output.allocationId.value = n + pair.2 := by
  have same := congrArg (fun identity : Format.ObjectId × Format.AllocationId × Format.ObjectState => identity.2.1.value) projected.identity
  exact same

theorem CopyProjection.state {heap : PreparedHeap n k} {state : Copying.EvacuationState n} (projected : CopyProjection heap state units pair output) :
    output.state = (match pair.1.destination with
      | .ordinary => .ordinaryDestination | .nonMoving => .nonmovingDestination) := by
  have same := congrArg (fun identity : Format.ObjectId × Format.AllocationId × Format.ObjectState => identity.2.2) projected.identity
  change output.state.withoutHeader = (match pair.1.destination with
    | .ordinary => .ordinaryDestination | .nonMoving => .nonmovingDestination) at same
  cases destination : pair.1.destination <;> cases shape : output.state <;>
    simp_all [Format.ObjectState.withoutHeader]

private theorem weakLayout_refl (units : Array Format.WeakUnit) : Format.WeakLayoutMatches units units := by
  refine ⟨rfl, ?_⟩
  intro pair member
  rw [List.zip_eq_zipWith, List.zipWith_self] at member
  obtain ⟨unit, _, rfl⟩ := List.mem_map.mp member
  exact ⟨rfl, rfl⟩

theorem OriginalProjection.weak_layout (projected : OriginalProjection heap state units collection before after) :
    Format.WeakLayoutMatches before.weak after.weak := by
  obtain ⟨object, _, _, _, weak⟩ := projected.object
  split at weak
  · rw [weak]
    exact weakLayout_refl _
  · exact weak.layout

theorem CopyProjection.is_destination {heap : PreparedHeap n k} {state : Copying.EvacuationState n}
    (projected : CopyProjection heap state units pair output) : Format.isDestination output.state = true := by
  rw [projected.state]
  cases pair.1.destination <;> rfl

theorem CopyProjection.is_young {heap : PreparedHeap n k} {state : Copying.EvacuationState n}
    (projected : CopyProjection heap state units pair output) : output.state.isYoung = false := by
  rw [projected.state]
  cases pair.1.destination <;> rfl

theorem SnapshotProjection.objects_size (projected : SnapshotProjection heap state units collection output) :
    output.objects.size = heap.initial.objects.size + state.copying.copies.length := by
  obtain ⟨originals, copies, objects, sources, destinations⟩ := projected.objects
  have same := congrArg List.length objects
  simpa only [Array.length_toList, List.length_append, sources.length, destinations.length,
    List.length_zipIdx] using same

theorem OriginalProjection.owner_layout (projected : OriginalProjection heap state units collection before after)
    (ordered : Format.FieldsBeforeEntries before.weak.toList) : Format.FieldsBeforeEntries after.weak.toList := by
  obtain ⟨_, _, _, _, weak⟩ := projected.object
  split at weak
  · rw [weak]
    exact ordered
  · exact weak.fields_before_entries ordered

theorem Format.ReferenceValid.mono (valid : Format.ReferenceValid before reference) (larger : before ≤ after) :
    Format.ReferenceValid after reference := by
  intro id member
  exact Nat.lt_of_lt_of_le (valid id member) larger

theorem Format.CellReferencesValid.mono (valid : Format.CellReferencesValid before cell) (larger : before ≤ after) :
    Format.CellReferencesValid after cell := by
  cases cell with
  | immediate value => trivial
  | reference target => exact Format.ReferenceValid.mono valid larger

theorem Format.WeakContentsReferencesValid.mono (valid : Format.WeakContentsReferencesValid before unit)
    (larger : before ≤ after) : Format.WeakContentsReferencesValid after unit := by
  cases unit with
  | field cell => exact Format.CellReferencesValid.mono valid larger
  | entry entry => exact ⟨Format.ReferenceValid.mono valid.1 larger, Format.ReferenceValid.mono valid.2.1 larger,
      Format.CellReferencesValid.mono valid.2.2 larger⟩

/-- Diagnostic source units remain bounded as well as repaired units. -/
theorem OriginalProjection.references_valid {heap : PreparedHeap n k} {state : Copying.EvacuationState n}
    (projected : OriginalProjection heap state units collection before after)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n)
    (initial : Format.AllocationCellReferencesValid n before) :
    Format.AllocationCellReferencesValid (n + state.copying.copies.length) after := by
  obtain ⟨_, _, _, strong, weak⟩ := projected.object
  refine ⟨strong.references_valid sources, ?_⟩
  intro unit member
  split at weak
  · rw [weak] at member
    exact (initial.2 unit member).mono (Nat.le_add_right _ _)
  · exact weak.references_valid sources unit member

theorem CopyProjection.references_valid {heap : PreparedHeap n k} {state : Copying.EvacuationState n}
    (projected : CopyProjection heap state units pair output)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n) :
    Format.AllocationCellReferencesValid (n + state.copying.copies.length) output :=
  ⟨projected.strong.references_valid sources, projected.weak.references_valid sources⟩

/-- Snapshot projection provides bounds for every root and stored cell. -/
theorem SnapshotProjection.references_valid {initial : Format.HeapSnapshot}
    {heap : PreparedHeap initial.objects.size k} {state : Copying.EvacuationState initial.objects.size}
    {units : Fin k → Copying.Weak.Unit initial.objects.size} {collection : Generational.CollectionKind}
    {output : Format.HeapSnapshot}
    (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial) :
    Format.HeapCellReferencesValid output := by
  have sources := prepared.original_bound valid
  have count : output.objects.size = initial.objects.size + state.copying.copies.length := by
    rw [projected.objects_size, prepared.snapshot]
  refine ⟨?_, ?_, ?_⟩
  · rw [count]
    exact projected.roots.references_valid sources
  · intro a member
    rw [count]
    obtain ⟨originals, copies, objects, sourceProjection, copyProjection⟩ := projected.objects
    rw [objects] at member
    rcases List.mem_append.mp member with original | destination
    · obtain ⟨before, stored, relation⟩ := ListProjection.after_exists sourceProjection original
      rw [prepared.snapshot] at stored
      exact relation.references_valid sources (valid.references.objects before stored)
    · obtain ⟨pair, _, relation⟩ := ListProjection.after_exists copyProjection destination
      exact relation.references_valid sources
  · intro unit member
    rw [count]
    exact projected.externalWeak.references_valid sources unit member

theorem OriginalProjection.unit_ids (projected : OriginalProjection heap state units collection before after) :
    after.weak.toList.map Format.WeakUnit.unitId = before.weak.toList.map Format.WeakUnit.unitId := by
  obtain ⟨_, _, _, _, weak⟩ := projected.object
  split at weak
  · rw [weak]
  · exact weak.ids

/-- Every owner layout remains ordered after capture, including diagnostic originals. -/
theorem SnapshotProjection.owner_layout {initial : Format.HeapSnapshot}
    {heap : PreparedHeap initial.objects.size k} {state : Copying.EvacuationState initial.objects.size}
    {units : Fin k → Copying.Weak.Unit initial.objects.size} {collection : Generational.CollectionKind}
    {output : Format.HeapSnapshot} (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial) :
    ∀ a ∈ output.objects.toList, Format.FieldsBeforeEntries a.weak.toList := by
  intro a member
  obtain ⟨originals, copies, objects, sourceProjection, copyProjection⟩ := projected.objects
  rw [objects] at member
  rcases List.mem_append.mp member with original | destination
  · obtain ⟨before, stored, relation⟩ := ListProjection.after_exists sourceProjection original
    rw [prepared.snapshot] at stored
    exact relation.owner_layout (valid.ownerLayout before stored)
  · obtain ⟨pair, _, relation⟩ := ListProjection.after_exists copyProjection destination
    have member := prepared.originals.member prepared.objectIds valid.identities pair.1.origin
    exact relation.weak.fields_before_entries (valid.ownerLayout _ member)

private theorem copyOrigins_unique {input : Copying.CopyInput n} {copying : Copying.CopyState n}
    (ledger : Copying.Ledger input copying) :
    (copying.copies.map (fun record => record.origin.val)).Nodup := by
  apply List.nodup_iff_eq_of_getElem_eq.mpr
  intro i j leftBound rightBound same
  have inLeft : i < copying.copies.length := by simpa using leftBound
  have inRight : j < copying.copies.length := by simpa using rightBound
  have origins : copying.copies[i].origin = copying.copies[j].origin :=
    Fin.ext (by simpa only [List.getElem_map] using same)
  have left := ledger.copies_forwarded i copying.copies[i] (List.getElem?_eq_getElem inLeft)
  have right := ledger.copies_forwarded j copying.copies[j] (List.getElem?_eq_getElem inRight)
  rw [origins] at left
  exact Option.some.inj (left.symm.trans right)

section
variable {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
  {state : Copying.EvacuationState initial.objects.size} {units : Fin k → Copying.Weak.Unit initial.objects.size}
  {collection : Generational.CollectionKind} {output : Format.HeapSnapshot}

/-- Copied logical IDs remain the origins' IDs, in destination allocation order. -/
private theorem capturedCopies_logical_ids (prepared : PreparedHeapProjection initial heap)
    (valid : Format.FreshWellFormed initial)
    (projected : ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx copies) :
    copies.map (fun a => Format.logicalOrdinal a.objectId) = state.copying.copies.map (fun record => record.origin.val) := by
  have mapped := projected.map_eq (fun pair => pair.1.origin.val) (fun a => Format.logicalOrdinal a.objectId) (by
    intro pair a relation
    rw [relation.object_id]
    exact prepared.originals.logical_id prepared.objectIds valid.identities pair.1.origin)
  have keys := congrArg (List.map (fun record : Copying.CopyRecord initial.objects.size => record.origin.val))
    (List.zipIdx_map_fst 0 state.copying.copies)
  simp only [List.map_map, Function.comp_def] at keys
  exact mapped.trans keys

/-- A copy ledger cannot emit duplicate logical copy identities. -/
theorem capturedCopies_unique (prepared : PreparedHeapProjection initial heap)
    (valid : Format.FreshWellFormed initial) (ledger : Copying.Ledger heap.copying state.copying)
    (projected : ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx copies) :
    (copies.map (fun a => Format.logicalOrdinal a.objectId)).Nodup := by
  rw [capturedCopies_logical_ids prepared valid projected]
  exact copyOrigins_unique ledger

/-- Logical destination IDs always name one of the original objects. -/
theorem CopyProjection.logical_bound (projected : CopyProjection heap state units pair a)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial) :
    Format.logicalOrdinal a.objectId < initial.objects.size := by
  rw [projected.object_id, prepared.originals.logical_id prepared.objectIds valid.identities pair.1.origin]
  exact pair.1.origin.isLt

/-- Source and destination physical IDs use one dense global sequence. -/
theorem SnapshotProjection.allocations_dense (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial) :
    output.objects.toList.map (fun a => a.allocationId.value) = List.range output.objects.size := by
  obtain ⟨originals, copies, objects, sources, destinations⟩ := projected.objects
  have sourceIds : originals.map (fun a => a.allocationId.value) = List.range initial.objects.size := by
    have ids := sources.map_eq (fun a => a.allocationId.value) (fun a => a.allocationId.value)
      (fun _ _ relation => congrArg Format.AllocationId.value relation.allocationId)
    rw [prepared.snapshot, valid.identities.allocations] at ids
    exact ids
  have copyIds := destinations.map_eq (fun pair => initial.objects.size + pair.2) (fun a => a.allocationId.value)
    (fun _ _ relation => relation.allocation_id)
  have numbered := congrArg (List.map (initial.objects.size + ·)) (List.zipIdx_map_snd 0 state.copying.copies)
  have indices : state.copying.copies.zipIdx.map (fun pair => initial.objects.size + pair.2) =
      (List.range state.copying.copies.length).map (initial.objects.size + ·) := by
    simpa only [List.map_map, Function.comp_def, List.range_eq_range'] using numbered
  rw [indices] at copyIds
  rw [objects, List.map_append, sourceIds, copyIds, ← List.range_add]
  rw [projected.objects_size, prepared.snapshot]

/-- Both original and copied names are canonical source names. -/
theorem SnapshotProjection.canonical (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial) :
    ∀ a ∈ output.objects.toList, Format.CanonicalObjectId a.objectId := by
  intro a member
  obtain ⟨originals, copies, objects, sources, destinations⟩ := projected.objects
  rw [objects] at member
  rcases List.mem_append.mp member with original | copied
  · obtain ⟨before, stored, relation⟩ := sources.after_exists original
    rw [relation.objectId]
    rw [prepared.snapshot] at stored
    exact valid.identities.canonical before stored
  · obtain ⟨pair, _, relation⟩ := destinations.after_exists copied
    rw [relation.object_id]
    exact valid.identities.canonical _ (prepared.originals.member prepared.objectIds valid.identities pair.1.origin)

/-- The original logical-ID permutation is preserved even when headers change. -/
theorem capturedOriginals_logical_ids (projected : ListProjection (OriginalProjection heap state units collection)
      heap.initial.objects.toList originals)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial) :
    (originals.map (fun a => Format.logicalOrdinal a.objectId)).Perm (List.range originals.length) := by
  have ids := projected.map_eq (fun a => Format.logicalOrdinal a.objectId) (fun a => Format.logicalOrdinal a.objectId)
    (fun _ _ relation => congrArg Format.logicalOrdinal relation.objectId)
  rw [ids, projected.length, prepared.snapshot]
  exact valid.identities.objects

/-- Destination layouts share source IDs; only source/external units contribute to density. -/
theorem SnapshotProjection.original_unit_ids (projected : SnapshotProjection heap state units collection output)
    (sources : ListProjection (OriginalProjection heap state units collection) heap.initial.objects.toList originals) :
    Format.originalUnitIds output originals = Format.freshUnitIds heap.initial := by
  have owned := sources.flatMap_eq
    (fun a => a.weak.toList.map (fun unit => unit.unitId.value))
    (fun a => a.weak.toList.map (fun unit => unit.unitId.value)) (by
      intro before after relation
      simpa only [List.map_map, Function.comp_def] using congrArg (List.map Format.UnitId.value) relation.unit_ids)
  have external := congrArg (List.map Format.UnitId.value) projected.externalWeak.ids
  simp only [List.map_map, Function.comp_def] at external
  simp only [Format.originalUnitIds, Format.freshUnitIds, List.map_append, List.map_flatMap, owned, external]

end

end CloverGC.Differential
