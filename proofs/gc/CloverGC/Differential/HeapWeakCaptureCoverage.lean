import CloverGC.Differential.HeapStrongCaptureCoverage
import CloverGC.Differential.HeapCaptureCompleteness

namespace CloverGC.Differential

private theorem unremembered_template_old (heap : PreparedHeap n k)
    (valid : Generational.Weak.WeakWellFormed ⟨heap.generational, heap.weak⟩)
    (owner : Fin n) (old : heap.generational.data.isYoung owner = false)
    (unremembered : owner ∉ heap.generational.data.rememberedOwners) (id : Fin k)
    (owned : id ∈ heap.weak.ownerUnits owner) (target : Fin n)
    (member : some target ∈ (heap.weak.templates id).targets) : heap.generational.data.isYoung target = false := by
  cases age : heap.generational.data.isYoung target with
  | false => rfl
  | true => exact False.elim (unremembered (valid.old_to_young_covered owner id target old owned member age))

private theorem initial_unit_old (heap : PreparedHeap n k)
    (valid : Generational.Weak.WeakWellFormed ⟨heap.generational, heap.weak⟩)
    (owner : Fin n) (old : heap.generational.data.isYoung owner = false)
    (unremembered : owner ∉ heap.generational.data.rememberedOwners) (id : Fin k)
    (owned : id ∈ heap.weak.ownerUnits owner) (reference : Copying.Reference n)
    (member : some reference ∈ (heap.weak.templates id).initial.targets) : NonYoungReference heap reference := by
  rw [Copying.Weak.Template.initial, Copying.Weak.targets_map] at member
  obtain ⟨original, inside, same⟩ := List.mem_map.mp member
  cases original with
  | none => simp at same
  | some target =>
      have equal : Copying.Reference.source target = reference := by simpa using same
      subst reference
      exact unremembered_template_old heap valid owner old unremembered id owned target inside

/-- Weak storage covered by the same remembered-owner set as strong fields. -/
structure WeakCaptureCoverage (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) : Prop where
  originals : ∀ owner, heap.generational.data.isYoung owner = false → owner ∉ heap.generational.data.rememberedOwners →
    ∀ id ∈ heap.weak.ownerUnits owner, ∀ reference, some reference ∈ (units id).targets → NonYoungReference heap reference
  copies : ∀ (index : Nat) (record : Copying.CopyRecord n), copying.copies[index]? = some record →
    ∀ id ∈ heap.weak.ownerUnits record.origin, ∀ reference, some reference ∈ (units id).targets → NonYoungReference heap reference

/-- Full repair preserves barrier coverage in unvisited units and repairs live owner storage safely. -/
theorem weakCaptureCoverage_full (heap : PreparedHeap n k)
    (valid : Generational.Weak.WeakWellFormed ⟨heap.generational, heap.weak⟩) :
    let state := Copying.Weak.collectWeak heap.copying heap.weak heap.roots
    WeakCaptureCoverage heap state.strong.copying state.units := by
  let state := Copying.Weak.collectWeak heap.copying heap.weak heap.roots
  change WeakCaptureCoverage heap state.strong.copying state.units
  have invariant := Copying.evacuate_invariant heap.copying heap.roots
  constructor
  · intro owner old unremembered id owned reference member
    by_cases registered : id ∈ state.registered
    · have safe := Copying.Weak.collection_disposal_safe heap.copying heap.weak heap.roots valid.storage id registered reference member
      exact canonical_reference_old heap _ reference safe
    · rw [full_unregistered_unchanged heap.copying heap.weak heap.roots valid.storage id registered] at member
      exact initial_unit_old heap valid owner old unremembered id owned reference member
  · intro index record allocated id owned reference member
    have coreAllocated : (Copying.evacuate heap.copying heap.roots).1.copying.copies[index]? = some record := by
      simpa only [state, Copying.Weak.collectWeak_strong] using allocated
    have forward := invariant.copying.ledger.copies_forwarded index record coreAllocated
    have live := ((Copying.evacuate_forwarded_iff heap.copying heap.roots record.origin).mp ⟨index, forward⟩).1
    have registered : id ∈ state.registered :=
      (Copying.Weak.collection_registered_iff heap.copying heap.weak heap.roots id).mpr (Or.inr ⟨record.origin, live, owned⟩)
    have safe := Copying.Weak.collection_disposal_safe heap.copying heap.weak heap.roots valid.storage id registered reference member
    exact canonical_reference_old heap _ reference safe

/-- Generational repair retains old weak targets and repairs every copied owner's units safely. -/
theorem weakCaptureCoverage_generational (heap : PreparedHeap n k)
    (valid : Generational.Weak.WeakWellFormed ⟨heap.generational, heap.weak⟩) (collection : Generational.CollectionKind) :
    let state := (Generational.Weak.collectWeak (preparedWeakInput heap valid collection)).1
    WeakCaptureCoverage heap state.strong.copying state.units := by
  let input := preparedWeakInput heap valid collection
  let state := (Generational.Weak.collectWeak input).1
  change WeakCaptureCoverage heap state.strong.copying state.units
  have invariant := Generational.evacuate_invariant input.strongInput
  have strong := congrArg Prod.fst (Generational.Weak.collectWeak_strong input)
  dsimp only at strong
  constructor
  · intro owner old unremembered id owned reference member
    by_cases registered : id ∈ state.registered
    · have safe := Generational.Weak.collection_registered_safe input id registered reference member
      exact generational_reference_old heap _ collection reference safe
    · rw [Generational.Weak.collection_unregistered_unchanged input id registered] at member
      exact initial_unit_old heap valid owner old unremembered id owned reference member
  · intro index record allocated id owned reference member
    have coreAllocated : (Generational.evacuate input.strongInput).1.copying.copies[index]? = some record := by
      simpa only [state, strong] using allocated
    have forward := invariant.tracking.copying.ledger.copies_forwarded index record coreAllocated
    have live := ((Generational.evacuate_forwarded_iff input.strongInput record.origin).mp ⟨index, forward⟩).1
    have safe := Generational.Weak.collection_retained_owner_safe input record.origin (Or.inr live) id owned reference member
    exact generational_reference_old heap _ collection reference safe

end CloverGC.Differential
