import CloverGC.Differential.HeapSnapshotStructure
import CloverGC.Differential.HeapCaptureCompleteness

namespace CloverGC.Differential

private theorem OriginalObject.unique (left : OriginalObject heap a before) (right : OriginalObject heap a after) : before = after := by
  obtain ⟨i, leftId, leftObject⟩ := left
  obtain ⟨j, rightId, rightObject⟩ := right
  have same : i = j := Fin.ext (leftId.trans rightId.symm)
  subst j
  exact leftObject.symm.trans rightObject

theorem OriginalObject.retained_record {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    {a : Format.Allocation} {object : Fin initial.objects.size}
    (mapped : OriginalObject heap a object) (prepared : PreparedHeapProjection initial heap)
    (valid : Format.FreshWellFormed initial) (member : a ∈ initial.objects.toList) : heap.originals.get object = a := by
  obtain ⟨other, otherMapped, stored⟩ := prepared.original_object valid a member
  rw [OriginalObject.unique mapped otherMapped]
  exact stored

private theorem capturedCopies.at {heap : PreparedHeap n k} {state : Copying.EvacuationState n}
    {units : Fin k → Copying.Weak.Unit n} (projected : ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx copies)
    {index : Nat} (allocated : state.copying.copies[index]? = some record) :
    ∃ output, copies[index]? = some output ∧ CopyProjection heap state units (record, index) output := by
  obtain ⟨bound, same⟩ := List.getElem?_eq_some_iff.mp allocated
  have sourceBound : index < state.copying.copies.zipIdx.length := by simpa using bound
  have targetBound : index < copies.length := by simpa only [projected.length] using sourceBound
  have relation := projected.getElem index sourceBound targetBound
  simp only [List.getElem_zipIdx, same, Nat.zero_add] at relation
  exact ⟨copies[index], List.getElem?_eq_getElem targetBound, relation⟩

private theorem find_by_key (key : α → Nat) (values : List α) (a : α)
    (unique : (values.map key).Nodup) (member : a ∈ values) :
    values.find? (fun b => key b == key a) = some a := by
  cases found : values.find? (fun b => key b == key a) with
  | none => exact False.elim (List.find?_eq_none.mp found a member (by simp))
  | some b =>
      have inside := List.mem_of_find?_eq_some found
      have keys : key b = key a := by simpa using List.find?_some found
      obtain ⟨i, inLeft, left⟩ := List.mem_iff_getElem.mp inside
      obtain ⟨j, inRight, right⟩ := List.mem_iff_getElem.mp member
      have positions : i = j := unique.eq_of_getElem_eq (by simpa using inLeft) (by simpa using inRight)
        (by simpa only [List.getElem_map, left, right] using keys)
      have same : b = a := left.symm.trans (positions ▸ right)
      exact congrArg some same

section
variable {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
  {state : Copying.EvacuationState initial.objects.size} {units : Fin k → Copying.Weak.Unit initial.objects.size}
  {copies : List Format.Allocation}

private theorem capturedCopies.forwarded (projected : ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx copies)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (ledger : Copying.Ledger heap.copying state.copying) (object : Fin initial.objects.size) (index : Nat)
    (forward : state.copying.forwarding object = some index) :
    ∃ record output, state.copying.copies[index]? = some record ∧ record.origin = object ∧
      copies.find? (fun a => Format.logicalOrdinal a.objectId == object.val) = some output ∧
      CopyProjection heap state units (record, index) output := by
  obtain ⟨record, allocated, origin⟩ := ledger.forwarding_valid object index forward
  obtain ⟨output, stored, relation⟩ := capturedCopies.at projected allocated
  have name : Format.logicalOrdinal output.objectId = object.val := by
    rw [relation.object_id, prepared.originals.logical_id prepared.objectIds valid.identities record.origin, origin]
  have member : output ∈ copies := List.mem_of_getElem? stored
  have unique := capturedCopies_unique prepared valid ledger projected
  have found := find_by_key (fun a : Format.Allocation => Format.logicalOrdinal a.objectId) copies output unique member
  simp only [name] at found
  exact ⟨record, output, allocated, origin, found, relation⟩

private theorem capturedCopies.unforwarded (projected : ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx copies)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (ledger : Copying.Ledger heap.copying state.copying) (object : Fin initial.objects.size)
    (forward : state.copying.forwarding object = none) :
    copies.find? (fun a => Format.logicalOrdinal a.objectId == object.val) = none := by
  apply List.find?_eq_none.mpr
  intro a member matched
  obtain ⟨pair, stored, relation⟩ := projected.after_exists member
  have allocated : state.copying.copies[pair.2]? = some pair.1 := List.mem_zipIdx_iff_getElem?.mp stored
  have name : pair.1.origin = object := by
    apply Fin.ext
    simpa only [relation.object_id, prepared.originals.logical_id prepared.objectIds valid.identities pair.1.origin,
      beq_iff_eq] using matched
  have actual := ledger.copies_forwarded _ _ allocated
  rw [name, forward] at actual
  contradiction

/-- Observed forwarding names exactly the matching destination with the source's
    placement policy and shared weak layout. -/
theorem OriginalProjection.forwarding_pair (projected : OriginalProjection heap state units collection before after)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (ready : StrongCaptureReady heap state collection) (member : before ∈ initial.objects.toList)
    (copiesProjected : ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx copies) :
    Format.AllocationPairValid after (copies.find? (fun a => Format.logicalOrdinal a.objectId == Format.logicalOrdinal after.objectId)) := by
  obtain ⟨object, mapped, shape, _, weak⟩ := projected.object
  have stored := OriginalObject.retained_record mapped prepared valid member
  have name : Format.logicalOrdinal after.objectId = object.val := by
    rw [projected.objectId, ← stored]
    exact prepared.originals.logical_id prepared.objectIds valid.identities object
  cases forward : state.copying.forwarding object with
  | none =>
      have missing := capturedCopies.unforwarded copiesProjected prepared valid ready.copying.ledger object forward
      rw [name, missing]
      have header : OriginalStateProjection before.state
          (.normal (decide (.source object ∈ state.copying.marked))) after.state := by
        simpa only [encodeHeader, forward] using shape
      cases before with
      | mk objectId allocationId sourceState strongLayout weakLayout =>
          cases after with
          | mk outId outAllocationId targetState targetStrong targetWeak =>
              dsimp only at header ⊢
              cases header <;> trivial
  | some index =>
      obtain ⟨record, copy, allocated, origin, found, copyProjected⟩ :=
        capturedCopies.forwarded copiesProjected prepared valid ready.copying.ledger object index forward
      rw [name, found]
      obtain ⟨moving, placement⟩ := ready.copying.ledger.copies_metadata index record allocated
      have selected := ready.selected index record allocated
      rw [origin] at moving placement selected
      have diagnostic : after.weak = before.weak := by
        simpa only [selected, moving, Bool.true_and, ↓reduceIte] using weak
      have layout := copyProjected.weak.layout
      rw [origin, stored, ← diagnostic] at layout
      have copyState := copyProjected.state
      have target : encodeReference heap (.copied index) = copy.allocationId :=
        congrArg Format.AllocationId.mk copyProjected.allocation_id.symm
      have kind := prepared.kinds object
      rw [stored] at kind
      change record.destination = (heap.generational.data.objectKind object).destination at placement
      generalize kindEq : heap.generational.data.objectKind object = objectKind at kind placement
      have header : OriginalStateProjection before.state
          (.forwarded (encodeReference heap (.copied index))) after.state := by
        simpa only [encodeHeader, forward] using shape
      cases before with
      | mk objectId allocationId sourceState strongLayout weakLayout =>
          cases after with
          | mk outId outAllocationId targetState targetStrong targetWeak =>
              dsimp only at header kind ⊢
              cases header
              all_goals
                simp only [ModelOriginalKind] at kind
                split at kind <;> try contradiction
                all_goals simp_all [Format.AllocationPairValid, Generational.OriginalKind.destination]
                all_goals rfl

/-- Every original in the completed snapshot has reciprocal forwarding metadata. -/
theorem capturedOriginals_forwarding_pairs
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (ready : StrongCaptureReady heap state collection)
    (sources : ListProjection (OriginalProjection heap state units collection) heap.initial.objects.toList originals)
    (destinations : ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx copies) :
    ∀ a ∈ originals, Format.AllocationPairValid a
      (copies.find? (fun b => Format.logicalOrdinal b.objectId == Format.logicalOrdinal a.objectId)) := by
  intro a member
  obtain ⟨before, stored, relation⟩ := sources.after_exists member
  rw [prepared.snapshot] at stored
  exact relation.forwarding_pair prepared valid ready stored destinations

end

theorem OriginalStateProjection.old_original (projected : OriginalStateProjection before header after) :
    Format.OldOriginal after ↔ Format.OldOriginal before := by cases projected <;> rfl

theorem OriginalProjection.old_original (projected : OriginalProjection heap state units collection before after) :
    Format.OldOriginal after.state ↔ Format.OldOriginal before.state := by
  obtain ⟨_, _, shape, _, _⟩ := projected.object
  exact shape.old_original

/-- Every physical source slot still names the corresponding captured original. -/
theorem SnapshotProjection.original_at (projected : SnapshotProjection heap state units collection output)
    {id : Nat} (found : heap.initial.objects[id]? = some before) :
    ∃ after, output.objects[id]? = some after ∧ OriginalProjection heap state units collection before after := by
  obtain ⟨originals, copies, objects, sources, _⟩ := projected.objects
  obtain ⟨bounded, initialAt⟩ := Array.getElem?_eq_some_iff.mp found
  have inSources : id < heap.initial.objects.toList.length := by simpa using bounded
  have inOutput : id < originals.length := by simpa only [sources.length] using inSources
  have relation := sources.getElem id inSources inOutput
  simp only [Array.getElem_toList, initialAt] at relation
  refine ⟨originals[id], ?_, relation⟩
  rw [← Array.getElem?_toList, objects, List.getElem?_append_left inOutput, List.getElem?_eq_getElem inOutput]

/-- Remembered entries continue to name old originals independently of collection reachability. -/
theorem SnapshotProjection.remembered_owners (projected : SnapshotProjection heap state units collection output)
    (owners : Format.RememberedOwnersValid heap.initial) : Format.RememberedOwnersValid output := by
  intro id member
  rw [projected.remembered] at member
  obtain ⟨before, found, old⟩ := owners id member
  obtain ⟨after, target, relation⟩ := projected.original_at found
  exact ⟨after, target, relation.old_original.mpr old⟩

theorem PreparedHeapProjection.original_lookup {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (object : Fin initial.objects.size) :
    heap.initial.objects[(heap.originals.get object).allocationId.value]? = some (heap.originals.get object) := by
  rw [prepared.snapshot]
  obtain ⟨physical, _, stored⟩ := prepared.originals.source prepared.objectIds valid.identities object
  rw [stored, valid.identities.allocation_at physical, Array.getElem?_eq_getElem physical.isLt]
  rfl

section
variable {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
  {state : Copying.EvacuationState initial.objects.size} {units : Fin k → Copying.Weak.Unit initial.objects.size}
  {collection : Generational.CollectionKind} {output : Format.HeapSnapshot}

theorem SnapshotProjection.copy_at (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) {index : Nat}
    (allocated : state.copying.copies[index]? = some record) :
    ∃ after, output.objects[initial.objects.size + index]? = some after ∧ CopyProjection heap state units (record, index) after := by
  obtain ⟨originals, copies, objects, sources, destinations⟩ := projected.objects
  obtain ⟨after, stored, relation⟩ := capturedCopies.at destinations allocated
  have count : originals.length = initial.objects.size := by simpa [prepared.snapshot] using sources.length
  refine ⟨after, ?_, relation⟩
  rw [← Array.getElem?_toList, objects, List.getElem?_append_right (by omega), count, Nat.add_sub_cancel_left]
  exact stored

/-- Safe encoded references cannot point to diagnostic young source records. -/
theorem SnapshotProjection.reference_not_young (projected : SnapshotProjection heap state units collection output)
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (reference : Copying.Reference initial.objects.size) (allocated : Copying.ValidReference state.copying reference)
    (old : NonYoungReference heap reference) (target : Format.Allocation)
    (found : output.objects[(encodeReference heap reference).value]? = some target) : target.state.isYoung = false := by
  cases reference with
  | source object =>
      obtain ⟨after, stored, relation⟩ := projected.original_at (prepared.original_lookup valid object)
      have same : after = target := Option.some.inj (stored.symm.trans found)
      rw [← same, relation.is_young, ← (prepared.kinds object).young]
      exact old
  | copied index =>
      obtain ⟨record, present⟩ := allocated
      obtain ⟨after, stored, relation⟩ := projected.copy_at prepared present
      have same : after = target := Option.some.inj (stored.symm.trans found)
      rw [← same]
      exact relation.is_young

end

theorem encodeUnit_targets (heap : PreparedHeap n k) (unit : Copying.Weak.Unit n) :
    (encodeUnit heap unit).targets = unit.targets.filterMap (fun reference => reference.map (encodeReference heap)) := by
  cases unit with
  | field cell =>
      cases cell with
      | immediate value => rfl
      | reference target => cases target <;> rfl
  | entry entry =>
      cases first : entry.firstGuard <;> cases second : entry.secondGuard <;> cases payload : entry.payload with
      | immediate value => simp [encodeUnit, first, second, payload, encodeCell, Format.WeakContents.targets,
          Copying.Weak.RepairUnit.targets, Copying.Weak.Cell.target]
      | reference target => cases target <;> simp [encodeUnit, first, second, payload, encodeCell, Format.WeakContents.targets,
          Copying.Weak.RepairUnit.targets, Copying.Weak.Cell.target]

/-- Every emitted non-null strong slot comes from the supplied model references. -/
theorem CellsProjection.reference_origin {heap : PreparedHeap n k} {copying : Copying.CopyState n}
    (projected : CellsProjection heap copying layout refs output) (id : Format.AllocationId)
    (member : Format.Cell.reference (some id) ∈ output) :
    ∃ reference ∈ refs, Copying.ValidReference copying reference ∧ encodeReference heap reference = id := by
  induction projected with
  | nil => cases member
  | immediate value tail ih =>
      apply ih
      simpa using member
  | null tail ih =>
      apply ih
      simpa using member
  | reference old reference safe tail ih =>
      simp only [List.mem_cons, Format.Cell.reference.injEq, Option.some.injEq] at member
      rcases member with same | rest
      · exact ⟨reference, by simp, safe, same.symm⟩
      · obtain ⟨target, inside, valid, encoded⟩ := ih rest
        exact ⟨target, by simp [inside], valid, encoded⟩

theorem UnitReferencesValid.target {copying : Copying.CopyState n} {unit : Copying.Weak.Unit n}
    (valid : UnitReferencesValid copying unit) (reference : Copying.Reference n)
    (member : some reference ∈ unit.targets) : Copying.ValidReference copying reference := by
  cases unit with
  | field cell =>
      cases cell with
      | immediate value => simp [Copying.Weak.RepairUnit.targets, Copying.Weak.Cell.target] at member
      | reference target =>
          cases target with
          | none => simp [Copying.Weak.RepairUnit.targets, Copying.Weak.Cell.target] at member
          | some actual =>
              have same : reference = actual := by simpa [Copying.Weak.RepairUnit.targets, Copying.Weak.Cell.target] using member
              exact same ▸ valid
  | entry entry =>
      simp only [Copying.Weak.RepairUnit.targets, List.mem_cons] at member
      rcases member with first | second | payload
      · exact valid.1 reference (by simpa [Option.mem_def, eq_comm] using first)
      · exact valid.2.1 reference (by simpa [Option.mem_def, eq_comm] using second)
      · cases shape : entry.payload with
        | immediate value => simp [Copying.Weak.Cell.target, shape] at payload
        | reference target =>
            cases target with
            | none => simp [Copying.Weak.Cell.target, shape] at payload
            | some actual =>
                have same : reference = actual := by simpa [Copying.Weak.Cell.target, shape] using payload
                have safe : Copying.ValidReference copying actual := by simpa only [shape, CellReferencesValid] using valid.2.2
                exact same ▸ safe

/-- Every encoded weak target comes from the same model unit and keeps its physical identity. -/
theorem UnitProjection.target_origin {heap : PreparedHeap n k} {copying : Copying.CopyState n}
    {units : Fin k → Copying.Weak.Unit n} (projected : UnitProjection heap copying units before after)
    (target : Format.AllocationId) (member : target ∈ after.contents.targets) :
    ∃ id : Fin k, id.val = before.unitId.value ∧ ∃ reference,
      some reference ∈ (units id).targets ∧ Copying.ValidReference copying reference ∧ encodeReference heap reference = target := by
  obtain ⟨id, identity, _, safe, rfl⟩ := projected
  rw [encodeUnit_targets] at member
  obtain ⟨original, inside, encoded⟩ := List.mem_filterMap.mp member
  cases original with
  | none => simp at encoded
  | some reference =>
      exact ⟨id, identity, reference, inside, safe.target reference inside, by simpa using encoded⟩

end CloverGC.Differential
