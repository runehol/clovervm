import CloverGC.Differential.HeapStrongCaptureValidity

namespace CloverGC.Differential

theorem immovable_kind_old (kind : Generational.OriginalKind) (immovable : kind.movable = false) :
    kind.isYoung = false := by cases kind <;> simp_all [Generational.OriginalKind.movable, Generational.OriginalKind.isYoung]

theorem canonical_reference_old (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (reference : Copying.Reference n) (safe : Copying.Canonical heap.copying copying reference) :
    NonYoungReference heap reference := by
  cases reference with
  | source object => exact immovable_kind_old _ safe
  | copied index => trivial

theorem generational_reference_old (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (collection : Generational.CollectionKind) (reference : Copying.Reference n)
    (safe : Generational.Safe (heap.collectionInput collection) copying reference) : NonYoungReference heap reference := by
  cases reference with
  | source object =>
      rcases safe with outside | stationary
      · cases collection with
        | full => simp [PreparedHeap.collectionInput, Generational.CollectionInput.selected] at outside
        | young => exact outside
      · exact immovable_kind_old _ stationary
  | copied index => trivial

private theorem unremembered_child_old (heap : PreparedHeap n k) (owner : Fin n)
    (old : heap.generational.data.isYoung owner = false) (unremembered : owner ∉ heap.generational.data.rememberedOwners)
    (child : Fin n) (edge : child ∈ heap.copying.heap.strongChildren owner) : heap.generational.data.isYoung child = false := by
  cases age : heap.generational.data.isYoung child with
  | false => rfl
  | true => exact False.elim (unremembered (heap.generational.valid.old_to_young_covered owner child old edge age))

private theorem canonical_child_old (heap : PreparedHeap n k) (copying : Copying.CopyState n) (child : Fin n)
    (old : heap.generational.data.isYoung child = false) : NonYoungReference heap (Copying.canonical copying child) := by
  cases forward : copying.forwarding child <;> simp [Copying.canonical, forward, NonYoungReference, old]

private theorem raw_children_old (heap : PreparedHeap n k) (owner : Fin n)
    (old : heap.generational.data.isYoung owner = false) (unremembered : owner ∉ heap.generational.data.rememberedOwners) :
    ∀ reference ∈ Copying.rawFields heap.copying owner, NonYoungReference heap reference := by
  intro reference member
  obtain ⟨child, edge, rfl⟩ := List.mem_map.mp member
  exact unremembered_child_old heap owner old unremembered child edge

private theorem canonical_children_old (heap : PreparedHeap n k) (copying : Copying.CopyState n) (owner : Fin n)
    (old : heap.generational.data.isYoung owner = false) (unremembered : owner ∉ heap.generational.data.rememberedOwners) :
    ∀ reference ∈ (heap.copying.heap.strongChildren owner).map (Copying.canonical copying), NonYoungReference heap reference := by
  intro reference member
  obtain ⟨child, edge, rfl⟩ := List.mem_map.mp member
  exact canonical_child_old heap copying child (unremembered_child_old heap owner old unremembered child edge)

/-- Strong storage covered by the single remembered-owner set after collection. -/
structure StrongCaptureCoverage (heap : PreparedHeap n k) (state : Copying.EvacuationState n) : Prop where
  originals : ∀ owner, heap.generational.data.isYoung owner = false → owner ∉ heap.generational.data.rememberedOwners →
    ∀ reference ∈ state.originalFields owner, NonYoungReference heap reference
  copies : ∀ (index : Nat) (record : Copying.CopyRecord n), state.copying.copies[index]? = some record →
    ∀ reference ∈ record.strongChildren, NonYoungReference heap reference

/-- Full collection preserves input barrier coverage in diagnostic sources and repairs copies safely. -/
theorem strongCaptureCoverage_full (heap : PreparedHeap n k) :
    StrongCaptureCoverage heap (Copying.evacuate heap.copying heap.roots).1 := by
  have invariant := Copying.evacuate_invariant heap.copying heap.roots
  constructor
  · intro owner old unremembered
    rw [invariant.fields.originals owner]
    split
    · unfold Copying.fieldPlan
      split
      · exact canonical_children_old heap _ owner old unremembered
      · exact raw_children_old heap owner old unremembered
    · exact raw_children_old heap owner old unremembered
  · intro index record allocated reference member
    have forward := invariant.copying.ledger.copies_forwarded index record allocated
    have live := ((Copying.evacuate_forwarded_iff heap.copying heap.roots record.origin).mp ⟨index, forward⟩).1
    have safe := (Copying.evacuate_disposal_safe heap.copying heap.roots).2 record.origin live reference (by
      simpa [Copying.canonical, forward, Copying.liveFields, allocated] using member)
    exact canonical_reference_old heap _ reference safe

/-- Generational collection derives the same coverage without scanning unremembered old owners. -/
theorem strongCaptureCoverage_generational (heap : PreparedHeap n k) (collection : Generational.CollectionKind) :
    StrongCaptureCoverage heap (Generational.evacuate (heap.collectionInput collection)).1 := by
  have invariant := Generational.evacuate_invariant (heap.collectionInput collection)
  constructor
  · intro owner old unremembered
    rw [invariant.fields.originals owner]
    unfold Generational.originalPlan
    split
    · exact canonical_children_old heap _ owner old unremembered
    · exact raw_children_old heap owner old unremembered
  · intro index record allocated reference member
    have forward := invariant.tracking.copying.ledger.copies_forwarded index record allocated
    have live := ((Generational.evacuate_forwarded_iff (heap.collectionInput collection) record.origin).mp ⟨index, forward⟩).1
    have safe := (Generational.evacuate_disposal_safe (heap.collectionInput collection)).2 record.origin (Or.inr live) reference (by
      simpa [Copying.canonical, forward, Copying.liveFields, allocated] using member)
    exact generational_reference_old heap _ collection reference safe

end CloverGC.Differential
