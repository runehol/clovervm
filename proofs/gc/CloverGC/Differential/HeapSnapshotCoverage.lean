import CloverGC.Differential.HeapWeakCaptureCoverage
import CloverGC.Differential.HeapSnapshotReferences

namespace CloverGC.Differential

theorem UnitsProjection.after_exists (projected : UnitsProjection heap copying units before after)
    (member : unit ∈ after.toList) : ∃ original ∈ before.toList, UnitProjection heap copying units original unit :=
  ListProjection.after_exists projected member

theorem ModelUnitIds.contains_id {units : List Format.WeakUnit} {ids : List (Fin k)} (projected : ModelUnitIds units ids) (member : unit ∈ units)
    (id : Fin k) (identity : id.val = unit.unitId.value) : id ∈ ids := by
  have inside : id.val ∈ ids.map Fin.val := by
    rw [projected]
    exact List.mem_map.mpr ⟨unit, member, identity.symm⟩
  obtain ⟨other, inside, same⟩ := List.mem_map.mp inside
  exact (Fin.ext same : other = id) ▸ inside

theorem PreparedHeapProjection.remembered_original {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (object : Fin initial.objects.size) (member : object ∈ heap.generational.data.rememberedOwners) :
    (heap.originals.get object).allocationId ∈ initial.remembered.toList := by
  obtain ⟨id, stored, relation⟩ := ListProjection.after_exists prepared.remembered member
  rw [prepared.originals.reference_id valid.identities id object relation]
  exact stored

private theorem allocation_member_lookup (heap : Format.HeapSnapshot)
    (dense : heap.objects.toList.map (fun a => a.allocationId.value) = List.range heap.objects.size)
    (a : Format.Allocation) (member : a ∈ heap.objects.toList) :
    heap.objects[a.allocationId.value]? = some a := by
  obtain ⟨i, within, same⟩ := List.mem_iff_getElem.mp member
  have bounded : i < heap.objects.size := by simpa using within
  have ids := congrArg (fun values : List Nat => values[i]?) dense
  have physical : a.allocationId.value = i := by
    simpa [List.getElem?_map, List.getElem?_eq_getElem within, same, List.getElem?_range bounded] using ids
  rw [physical, Array.getElem?_eq_getElem bounded]
  exact congrArg some same

/-- A young output record belongs to the original prefix, never a destination. -/
theorem SnapshotProjection.young_target_original (projected : SnapshotProjection heap state units collection output)
    {index : Nat} (found : output.objects[index]? = some target) (young : target.state.isYoung = true) :
    ∃ before, heap.initial.objects[index]? = some before ∧ OriginalProjection heap state units collection before target := by
  obtain ⟨originals, copies, objects, sources, destinations⟩ := projected.objects
  by_cases original : index < heap.initial.objects.size
  · let before := heap.initial.objects[index]
    have stored : heap.initial.objects[index]? = some before := Array.getElem?_eq_getElem original
    obtain ⟨after, captured, relation⟩ := projected.original_at stored
    have same : after = target := Option.some.inj (captured.symm.trans found)
    exact ⟨before, stored, same ▸ relation⟩
  · have bound : originals.length ≤ index := by simpa [sources.length] using Nat.le_of_not_lt original
    rw [← Array.getElem?_toList, objects, List.getElem?_append_right bound] at found
    have member := List.mem_of_getElem? found
    obtain ⟨pair, _, relation⟩ := destinations.after_exists member
    have old := relation.is_young
    simp [young] at old

section
variable {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
  {state : Copying.EvacuationState initial.objects.size} {units : Fin k → Copying.Weak.Unit initial.objects.size}
  {collection : Generational.CollectionKind} {output : Format.HeapSnapshot}

private theorem owner_unremembered (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (relation : OriginalProjection heap state units collection before after) (object : Fin initial.objects.size)
    (stored : heap.originals.get object = before) (missing : after.allocationId ∉ output.remembered.toList) :
    object ∉ heap.generational.data.rememberedOwners := by
  intro member
  have remembered := prepared.remembered_original valid object member
  apply missing
  simpa only [projected.remembered, prepared.snapshot, relation.allocationId, stored] using remembered

/-- Every emitted old-to-young strong edge retains its input remembered owner. -/
theorem SnapshotProjection.strong_covered (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (coverage : StrongCaptureCoverage heap state) : Format.StrongRememberedCovered output := by
  intro owner member old id edge target found young
  by_cases missing : owner.allocationId.value ∈ output.remembered.toList.map Format.AllocationId.value
  · exact missing
  ·
    have missingId : owner.allocationId ∉ output.remembered.toList := by
      intro inside
      exact missing (List.mem_map.mpr ⟨owner.allocationId, inside, rfl⟩)
    obtain ⟨originals, copies, objects, sources, destinations⟩ := projected.objects
    rw [objects] at member
    rcases List.mem_append.mp member with original | copied
    · obtain ⟨before, present, relation⟩ := sources.after_exists original
      rw [prepared.snapshot] at present
      obtain ⟨object, mapped, _, strong, _⟩ := relation.object
      have stored := mapped.retained_record prepared valid present
      have age : heap.generational.data.isYoung object = false := by
        change (heap.generational.data.objectKind object).isYoung = false
        rw [(prepared.kinds object).young, stored, ← relation.is_young]
        exact old
      have unremembered := owner_unremembered projected prepared valid relation object stored missingId
      obtain ⟨reference, inside, allocated, encoded⟩ := strong.reference_origin id edge
      have oldRef := coverage.originals object age unremembered reference inside
      have targetOld := projected.reference_not_young prepared valid reference allocated oldRef target (by rw [encoded]; exact found)
      simp [young] at targetOld
    · obtain ⟨pair, present, relation⟩ := destinations.after_exists copied
      have allocated : state.copying.copies[pair.2]? = some pair.1 := List.mem_zipIdx_iff_getElem?.mp present
      obtain ⟨reference, inside, validRef, encoded⟩ := relation.strong.reference_origin id edge
      have oldRef := coverage.copies _ _ allocated reference inside
      have targetOld := projected.reference_not_young prepared valid reference validRef oldRef target (by rw [encoded]; exact found)
      simp [young] at targetOld
/-- Weak old-to-young edges use the same remembered-owner entries as strong slots. -/
theorem SnapshotProjection.weak_covered (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (coverage : WeakCaptureCoverage heap state.copying units) : Format.WeakRememberedCovered output := by
  intro owner member old unit owned id edge target present identity young
  have found : output.objects[id.value]? = some target := by
    rw [← identity]
    exact allocation_member_lookup output (projected.allocations_dense prepared valid) target present
  by_cases missing : owner.allocationId ∈ output.remembered.toList
  · exact missing
  · obtain ⟨originals, copies, objects, sources, destinations⟩ := projected.objects
    rw [objects] at member
    rcases List.mem_append.mp member with original | copied
    · obtain ⟨before, storedBefore, relation⟩ := sources.after_exists original
      rw [prepared.snapshot] at storedBefore
      obtain ⟨object, mapped, _, _, weak⟩ := relation.object
      have stored := mapped.retained_record prepared valid storedBefore
      have age : heap.generational.data.isYoung object = false := by
        change (heap.generational.data.objectKind object).isYoung = false
        rw [(prepared.kinds object).young, stored, ← relation.is_young]
        exact old
      have unremembered := owner_unremembered projected prepared valid relation object stored missing
      split at weak
      · have beforeUnit : unit ∈ before.weak.toList := by simpa only [weak] using owned
        obtain ⟨beforeTarget, targetLookup, targetRelation⟩ := projected.young_target_original found young
        have targetInside : beforeTarget ∈ initial.objects.toList := by
          apply List.mem_of_getElem?
          simpa only [Array.getElem?_toList, prepared.snapshot] using targetLookup
        have remembered := valid.remembered.weak before storedBefore (relation.is_young.symm.trans old)
          unit beforeUnit id edge beforeTarget targetInside (targetRelation.allocationId.symm.trans identity)
          (targetRelation.is_young.symm.trans young)
        simpa only [projected.remembered, prepared.snapshot, relation.allocationId] using remembered
      · obtain ⟨beforeUnit, beforeOwned, unitRelation⟩ := weak.after_exists owned
        obtain ⟨unitId, unitIdentity, reference, targetMember, allocated, encoded⟩ := unitRelation.target_origin id edge
        have ownerIds := prepared.ownerUnits object
        rw [stored] at ownerIds
        have coreOwned := ownerIds.contains_id beforeOwned unitId unitIdentity
        have oldRef := coverage.originals object age unremembered unitId coreOwned reference targetMember
        have targetOld := projected.reference_not_young prepared valid reference allocated oldRef target (by rw [encoded]; exact found)
        simp [young] at targetOld
    · obtain ⟨pair, stored, relation⟩ := destinations.after_exists copied
      have allocated : state.copying.copies[pair.2]? = some pair.1 := List.mem_zipIdx_iff_getElem?.mp stored
      obtain ⟨beforeUnit, beforeOwned, unitRelation⟩ := relation.weak.after_exists owned
      obtain ⟨unitId, unitIdentity, reference, targetMember, validRef, encoded⟩ := unitRelation.target_origin id edge
      have coreOwned := (prepared.ownerUnits pair.1.origin).contains_id beforeOwned unitId unitIdentity
      have oldRef := coverage.copies _ _ allocated unitId coreOwned reference targetMember
      have targetOld := projected.reference_not_young prepared valid reference validRef oldRef target (by rw [encoded]; exact found)
      simp [young] at targetOld

/-- Capture preserves the single remembered set and all its semantic obligations. -/
theorem SnapshotProjection.remembered_valid (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (strong : StrongCaptureCoverage heap state) (weak : WeakCaptureCoverage heap state.copying units) :
    Format.RememberedValid output := by
  refine ⟨?_, ?_, projected.strong_covered prepared valid strong, projected.weak_covered prepared valid weak⟩
  · simpa only [projected.remembered, prepared.snapshot] using valid.remembered.unique
  · apply projected.remembered_owners
    simpa only [prepared.snapshot] using valid.remembered.owners

/-- Every semantically captured complete collection satisfies the independent
    completed-heap format, without assuming any output checker succeeds. -/
theorem SnapshotProjection.completed_wellFormed (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (ready : StrongCaptureReady heap state collection) (strong : StrongCaptureCoverage heap state)
    (weak : WeakCaptureCoverage heap state.copying units) :
    ∃ originals copies, Format.CompletedWellFormed output originals copies := by
  obtain ⟨originals, copies, objects, sources, destinations⟩ := projected.objects
  refine ⟨originals, copies, ?_⟩
  refine {
    records := objects
    originalStates := ?_
    destinationStates := ?_
    allocations := projected.allocations_dense prepared valid
    canonical := projected.canonical prepared valid
    originalIds := capturedOriginals_logical_ids sources prepared valid
    copyIds := capturedCopies_unique prepared valid ready.copying.ledger destinations
    copyBounds := ?_
    references := projected.references_valid prepared valid
    ownerLayout := projected.owner_layout prepared valid
    units := ?_
    remembered := projected.remembered_valid prepared valid strong weak
    pairs := capturedOriginals_forwarding_pairs prepared valid ready sources destinations
  }
  · intro a member
    obtain ⟨_, _, relation⟩ := sources.after_exists member
    exact relation.is_original
  · intro a member
    obtain ⟨_, _, relation⟩ := destinations.after_exists member
    exact relation.is_destination
  · intro a member
    obtain ⟨_, _, relation⟩ := destinations.after_exists member
    simpa only [sources.length, prepared.snapshot, Array.length_toList] using relation.logical_bound prepared valid
  · simpa only [projected.original_unit_ids sources, prepared.snapshot, Format.FreshUnitIds] using valid.units

end
end CloverGC.Differential
