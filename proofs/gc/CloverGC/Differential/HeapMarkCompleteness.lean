import CloverGC.Differential.HeapPreparationCompleteness
import CloverGC.MarkProofs

namespace CloverGC.Differential

private theorem originalState_normal_complete (before : Format.ObjectState) (fresh : Format.FreshState before)
    (marked : Bool) : ∃ after, originalState before (.normal marked) = .ok after := by
  cases before with
  | youngOriginal policy header => exact ⟨.youngOriginal policy (.normal marked), rfl⟩
  | oldOriginal policy header => exact ⟨.oldOriginal policy (.normal marked), rfl⟩
  | nonmovingOriginal wasMarked => exact ⟨.nonmovingOriginal marked, rfl⟩
  | ordinaryDestination | nonmovingDestination => cases fresh

/-- Complete marking capture succeeds on prepared input, preserving its full snapshot projection. -/
theorem captureMark_complete (initial : Format.HeapSnapshot) (valid : Format.FreshWellFormed initial)
    (heap : PreparedHeap initial.objects.size k) (projected : PreparedHeapProjection initial heap)
    (state : MarkState initial.objects.size) (complete : state.pending = []) :
    ∃ output, captureMark heap state = .ok output ∧ MarkSnapshotProjection heap state output := by
  obtain ⟨objects, checked, _⟩ := mapM_success heap.initial.objects.toList (captureMarkedOriginal heap state)
    (fun _ _ => True) (by
      intro a member
      rw [projected.snapshot] at member
      have present : a.allocationId.value ∈ List.range initial.objects.size := by
        rw [← valid.identities.allocations]
        exact List.mem_map.mpr ⟨a, member, rfl⟩
      let physical : Fin initial.objects.size := ⟨a.allocationId.value, List.mem_range.mp present⟩
      let object := heap.objectOfAllocation.get physical
      have reference : originalReference heap.objectOfAllocation a.allocationId = .ok object :=
        (originalReference_projects _ _ _).mpr ⟨physical, rfl, rfl⟩
      obtain ⟨after, updated⟩ := originalState_normal_complete a.state (valid.states a member) (decide (object ∈ state.marked))
      let output := { a with state := after }
      refine ⟨output, ?_, trivial⟩
      simp [captureMarkedOriginal, reference, updated, output, bind, Except.bind, pure, Except.pure])
  let output := { heap.initial with objects := objects.toArray }
  have captured : captureMark heap state = .ok output := by
    simp [captureMark, complete, checked, output, bind, Except.bind, pure, Except.pure]
  exact ⟨output, captured, captureMark_projects heap state output captured⟩

/-- Full marking succeeds for prepared inputs; closure is supplied by the core proof. -/
theorem collectPrepared_mark_complete (initial : Format.HeapSnapshot) (valid : Format.FreshWellFormed initial)
    (heap : PreparedHeap initial.objects.size k) (projected : PreparedHeapProjection initial heap) :
    ∃ output, collectPrepared heap .mark .full = .ok output ∧
      MarkSnapshotProjection heap (CloverGC.collect heap.copying.heap heap.roots).1 output := by
  obtain ⟨output, captured, specification⟩ := captureMark_complete initial valid heap projected
    (CloverGC.collect heap.copying.heap heap.roots).1 (CloverGC.collect_complete _ _)
  exact ⟨output, captured, specification⟩

/-- A valid wire heap runs full marking successfully with exact prepared/captured correspondence. -/
theorem runCollection_mark_complete (input : Format.HeapSnapshot)
    (valid : Format.FreshWellFormed (Format.orderedHeap input)) :
    ∃ k, ∃ heap : PreparedHeap (Format.orderedHeap input).objects.size k, ∃ output,
      PreparedHeapProjection (Format.orderedHeap input) heap ∧
      runCollection input .mark .full = .ok output ∧
      MarkSnapshotProjection heap (CloverGC.collect heap.copying.heap heap.roots).1 output := by
  obtain ⟨k, heap, prepared, projected⟩ := prepareHeap_complete input valid
  obtain ⟨output, collected, specification⟩ := collectPrepared_mark_complete (Format.orderedHeap input) valid heap projected
  refine ⟨k, heap, output, projected, ?_, specification⟩
  simp [runCollection, prepared, collected, bind, Except.bind]

end CloverGC.Differential
