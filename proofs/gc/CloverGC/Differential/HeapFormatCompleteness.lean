import CloverGC.Differential.HeapCompletedFormatCompleteness

namespace CloverGC.Differential.Format

/-- A fresh heap is a completed snapshot with all records in the original prefix
    and no destinations. Freshness remains an independent input constraint. -/
theorem FreshWellFormed.completed (valid : FreshWellFormed heap) :
    CompletedWellFormed heap heap.objects.toList [] := by
  refine {
    records := by simp
    originalStates := fun a member => FreshState.not_destination (valid.states a member)
    destinationStates := by simp
    allocations := valid.identities.allocations
    canonical := valid.identities.canonical
    originalIds := by simpa using valid.identities.objects
    copyIds := by simp
    copyBounds := by simp
    references := valid.references
    ownerLayout := valid.ownerLayout
    units := by simpa [originalUnitIds, freshUnitIds, FreshUnitIds] using valid.units
    remembered := valid.remembered
    pairs := ?_ }
  intro a member
  have fresh := valid.states a member
  cases state : a.state <;> simp_all [FreshState, AllocationPairValid]
  all_goals split at fresh <;> simp_all

/-- Every independently well-formed fresh snapshot passes structural validation. -/
theorem validateStructure_fresh_complete (heap : HeapSnapshot) (valid : FreshWellFormed heap) :
    validateStructure heap = .ok () :=
  validateStructure_completed_complete heap heap.objects.toList [] valid.completed

/-- Fresh preparation accepts every semantically valid ordered input and returns
    that exact normalized heap. -/
theorem prepareFreshHeap_complete (input : HeapSnapshot)
    (valid : FreshWellFormed (orderedHeap input)) :
    prepareFreshHeap input = .ok (orderedHeap input) := by
  have structural := validateStructure_fresh_complete (orderedHeap input) valid
  have fresh := (checkFreshHeap_correct _).mpr valid.states
  simp [prepareFreshHeap, validateAndOrder, structural, valid.remembered.weak, fresh,
    bind, Except.bind, pure, Except.pure]

/-- Fresh preparation accepts exactly the independent semantic input domain. -/
theorem prepareFreshHeap_correct (input : HeapSnapshot) :
    prepareFreshHeap input = .ok (orderedHeap input) ↔ FreshWellFormed (orderedHeap input) :=
  ⟨prepareFreshHeap_wellFormed input (orderedHeap input), prepareFreshHeap_complete input⟩

end CloverGC.Differential.Format
