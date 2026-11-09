import CloverGC.Differential.HeapFormat
import Init.Data.Array.Sort.Lemmas

namespace CloverGC.Differential.Format

private theorem orderedHeap_coverage (input : HeapSnapshot) :
    WeakRememberedCovered (orderedHeap input) ↔ WeakRememberedCovered input := by
  simp only [WeakRememberedCovered, orderedHeap, ← Array.mem_def, Array.mem_mergeSort]

private theorem validateAndOrder_value (input : HeapSnapshot)
    (checked : { heap : HeapSnapshot // WeakRememberedCovered heap })
    (accepted : validateAndOrder input = .ok checked) : checked.val = orderedHeap input := by
  unfold validateAndOrder at accepted
  cases structureResult : validateStructure (orderedHeap input) with
  | error message => simp [structureResult, bind, Except.bind] at accepted
  | ok value =>
      simp only [structureResult, bind, Except.bind] at accepted
      split at accepted
      · simp only [pure, Except.pure, Except.ok.injEq] at accepted
        cases accepted
        rfl
      · contradiction

/-- Successful public validation certifies coverage of the supplied snapshot. -/
theorem validateHeap_weak_covered (heap : HeapSnapshot)
    (accepted : validateHeap heap = .ok ()) : WeakRememberedCovered heap := by
  unfold validateHeap at accepted
  cases result : validateAndOrder heap with
  | error message => simp [result, bind, Except.bind] at accepted
  | ok checked =>
      have covered := checked.property
      rw [validateAndOrder_value heap checked result] at covered
      exact (orderedHeap_coverage heap).mp covered

private theorem prepareFreshHeap_checked (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) :
    ∃ checked, validateAndOrder input = .ok checked ∧ checked.val = heap := by
  unfold prepareFreshHeap at accepted
  cases result : validateAndOrder input with
  | error message => simp [result, bind, Except.bind] at accepted
  | ok checked =>
      cases fresh : checkFreshHeap checked.val with
      | error message => simp [result, fresh, bind, Except.bind] at accepted
      | ok value =>
          simp [result, fresh, bind, Except.bind, pure, Except.pure] at accepted
          exact ⟨checked, rfl, accepted⟩

/-- Successful fresh preparation certifies coverage of the returned snapshot. -/
theorem prepareFreshHeap_weak_covered (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) : WeakRememberedCovered heap := by
  obtain ⟨checked, _, same⟩ := prepareFreshHeap_checked input heap accepted
  rw [← same]
  exact checked.property

/-- Fresh preparation only orders allocations; none are removed or replaced. -/
theorem prepareFreshHeap_objects_perm (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) :
    heap.objects.toList.Perm input.objects.toList := by
  obtain ⟨checked, validated, same⟩ := prepareFreshHeap_checked input heap accepted
  rw [← same, validateAndOrder_value input checked validated]
  exact Array.perm_iff_toList_perm.mp Array.mergeSort_perm

end CloverGC.Differential.Format
