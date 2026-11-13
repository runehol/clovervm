import CloverGC.Differential.HeapObjectPairCompleteness
import CloverGC.Differential.HeapPreparationCompleteness

namespace CloverGC.Differential

/-- Physical-to-logical conversion agrees at every allocation slot. -/
theorem PhysicalObjectMap.ordinal_at (mapped : PhysicalObjectMap initial objects)
    (physical : Fin initial.objects.size) :
    (objects.get physical).val = Format.logicalOrdinal (initial.objects.toVector.get physical).objectId := by
  change objects[physical.val].val = Format.logicalOrdinal initial.objects[physical.val].objectId
  have same := congrArg (fun values : List Nat => values[physical.val]?) mapped
  have within : physical.val < initial.objects.toList.length := by simp
  simpa [List.getElem?_map, List.getElem?_eq_getElem within,
    List.getElem?_eq_getElem (show physical.val < objects.toList.length by simp)] using same

/-- The logical table retains the canonical logical identity at each index. -/
theorem OriginalsIndexed.logical_id (indexed : OriginalsIndexed initial objects originals)
    (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial) (object : Fin initial.objects.size) :
    Format.logicalOrdinal (originals.get object).objectId = object.val := by
  obtain ⟨physical, same, stored⟩ := indexed.source mapped ids object
  rw [stored]
  exact (mapped.ordinal_at physical).symm.trans (congrArg Fin.val same)

/-- Every original reference encodes within the physical source prefix. -/
theorem PreparedHeapProjection.original_bound (projected : PreparedHeapProjection initial heap)
    (valid : Format.FreshWellFormed initial) (object : Fin initial.objects.size) :
    (heap.originals.get object).allocationId.value < initial.objects.size := by
  obtain ⟨physical, _, stored⟩ := projected.originals.source projected.objectIds valid.identities object
  rw [stored, valid.identities.allocation_at physical]
  exact physical.isLt

/-- An input allocation has one corresponding logical index and exact retained record. -/
theorem PreparedHeapProjection.original_object (projected : PreparedHeapProjection initial heap)
    (valid : Format.FreshWellFormed initial) (a : Format.Allocation) (member : a ∈ initial.objects.toList) :
    ∃ object, OriginalObject heap a object ∧ heap.originals.get object = a := by
  obtain ⟨i, within, same⟩ := List.mem_iff_getElem.mp member
  let physical : Fin initial.objects.size := ⟨i, by simpa using within⟩
  have record : initial.objects.toVector.get physical = a := same
  have ordinal : physical.val = a.allocationId.value := by
    rw [← record, valid.identities.allocation_at physical]
  exact ⟨heap.objectOfAllocation.get physical, ⟨physical, ordinal, rfl⟩,
    (projected.originals.at physical).trans record⟩


/-- Every valid model reference encodes inside the combined source/copy allocation space. -/
theorem encodeReference_bound (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n)
    (reference : Copying.Reference n) (safe : Copying.ValidReference copying reference) :
    (encodeReference heap reference).value < n + copying.copies.length := by
  cases reference with
  | source object => have bounded := sources object; simp only [encodeReference]; omega
  | copied index =>
      obtain ⟨record, allocated⟩ := safe
      have bounded := (List.getElem?_eq_some_iff.mp allocated).1
      simp only [encodeReference]
      omega

theorem encodeGuard_valid (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n)
    (guard : Option (Copying.Reference n)) (safe : GuardReferencesValid copying guard) :
    Format.ReferenceValid (n + copying.copies.length) (guard.map (encodeReference heap)) := by
  cases guard with
  | none => simp [Format.ReferenceValid]
  | some reference =>
      have valid := encodeReference_bound heap copying sources reference (safe reference (by simp))
      simpa [Format.ReferenceValid] using valid

theorem encodeCell_valid (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n)
    (cell : Copying.Weak.Cell (Copying.Reference n)) (safe : CellReferencesValid copying cell) :
    Format.CellReferencesValid (n + copying.copies.length) (encodeCell heap cell) := by
  cases cell with
  | immediate value => trivial
  | reference target =>
      cases target with
      | none => simp [encodeCell, Format.CellReferencesValid, Format.ReferenceValid]
      | some reference =>
          simpa [encodeCell, Format.CellReferencesValid, Format.ReferenceValid] using
            encodeReference_bound heap copying sources reference safe

theorem encodeUnit_valid (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n)
    (unit : Copying.Weak.Unit n) (safe : UnitReferencesValid copying unit) :
    Format.WeakContentsReferencesValid (n + copying.copies.length) (encodeUnit heap unit) := by
  cases unit with
  | field cell => exact encodeCell_valid heap copying sources cell safe
  | entry entry => exact ⟨encodeGuard_valid heap copying sources _ safe.1,
      encodeGuard_valid heap copying sources _ safe.2.1, encodeCell_valid heap copying sources _ safe.2.2⟩

/-- Slot projection establishes physical reference bounds throughout the emitted cells. -/
theorem CellsProjection.references_valid {heap : PreparedHeap n k} {copying : Copying.CopyState n} (projected : CellsProjection heap copying layout refs output)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n) :
    ∀ cell ∈ output, Format.CellReferencesValid (n + copying.copies.length) cell := by
  induction projected with
  | nil => simp
  | immediate value tail ih => simpa [Format.CellReferencesValid] using ih
  | null tail ih => simpa [Format.CellReferencesValid, Format.ReferenceValid] using ih
  | reference old reference safe tail ih =>
      have bounded := encodeReference_bound heap copying sources reference safe
      simpa [Format.CellReferencesValid, Format.ReferenceValid] using And.intro bounded ih

theorem UnitProjection.layout (projected : UnitProjection heap copying units before after) :
    after.unitId = before.unitId ∧ after.contents.isField = before.contents.isField := by
  obtain ⟨id, _, kind, _, rfl⟩ := projected
  refine ⟨rfl, ?_⟩
  cases left : before.contents <;> cases right : units id <;>
    simp_all [UnitKindMatches, encodeUnit, Format.WeakContents.isField]

private theorem unitList_layout (projected : ListProjection (UnitProjection heap copying units) before after) :
    ∀ pair ∈ before.zip after, pair.1.unitId = pair.2.unitId ∧
      pair.1.contents.isField = pair.2.contents.isField := by
  induction projected with
  | nil => simp
  | cons unit tail ih =>
      intro pair member
      simp only [List.zip_cons_cons, List.mem_cons] at member
      rcases member with rfl | rest
      · exact ⟨unit.layout.1.symm, unit.layout.2.symm⟩
      · exact ih pair rest

/-- Captured weak storage preserves source/copy layout compatibility. -/
theorem UnitsProjection.layout (projected : UnitsProjection heap copying units before after) :
    Format.WeakLayoutMatches before after := ⟨projected.size.symm, unitList_layout projected⟩

/-- Capturing unit contents cannot change owner field-before-entry ordering. -/
theorem UnitsProjection.fields_before_entries (projected : UnitsProjection heap copying units before after)
    (ordered : Format.FieldsBeforeEntries before.toList) : Format.FieldsBeforeEntries after.toList := by
  have kinds := ListProjection.map_eq projected (fun unit => unit.contents.isField)
    (fun unit => unit.contents.isField) (fun _ _ unit => unit.layout.2)
  have source : (before.toList.map (fun unit => unit.contents.isField)).Pairwise (fun a b => b = true → a = true) :=
    List.pairwise_map.mpr ordered
  rw [← kinds] at source
  exact List.pairwise_map.mp source

/-- Each encoded unit is reference-valid in the combined allocation space. -/
theorem UnitProjection.references_valid {heap : PreparedHeap n k} {copying : Copying.CopyState n}
    (projected : UnitProjection heap copying units before after)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n) :
    Format.WeakContentsReferencesValid (n + copying.copies.length) after.contents := by
  obtain ⟨id, _, _, safe, rfl⟩ := projected
  exact encodeUnit_valid heap copying sources (units id) safe

theorem UnitsProjection.references_valid {heap : PreparedHeap n k} {copying : Copying.CopyState n}
    (projected : UnitsProjection heap copying units before after)
    (sources : ∀ object, (heap.originals.get object).allocationId.value < n) :
    ∀ unit ∈ after.toList, Format.WeakContentsReferencesValid (n + copying.copies.length) unit.contents := by
  intro unit member
  obtain ⟨_, _, relation⟩ := ListProjection.after_exists projected member
  exact relation.references_valid sources

end CloverGC.Differential
