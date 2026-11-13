import CloverGC.Differential.HeapSnapshotCoverage
import CloverGC.Differential.HeapMarkCompleteness

namespace CloverGC.Differential

section
variable {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
  {state : Copying.EvacuationState initial.objects.size} {units : Fin k → Copying.Weak.Unit initial.objects.size}
  {collection : Generational.CollectionKind}

/-- Complete strong/weak state and input invariants suffice for actual capture,
    including the final independent output validation. -/
theorem captureStrong_complete (prepared : PreparedHeapProjection initial heap)
    (valid : Format.FreshWellFormed initial) (ready : StrongCaptureReady heap state collection)
    (weakReady : WeakCaptureReady heap state.copying units) (strong : StrongCaptureCoverage heap state)
    (weak : WeakCaptureCoverage heap state.copying units) :
    ∃ output, captureStrong heap state units collection = .ok output ∧
      SnapshotProjection heap state units collection output ∧
      ∃ originals copies, Format.CompletedWellFormed output originals copies := by
  have rootModel : ModelStrongReferences heap.objectOfAllocation heap.initial.roots.toList heap.roots := by
    simpa only [prepared.snapshot] using prepared.roots
  obtain ⟨roots, rootsChecked, rootsProjected⟩ := mergeReferences_succeeds_of_model heap state.copying rootModel state.roots
    ready.roots.1 ready.roots.2
  obtain ⟨originals, originalsChecked, originalsProjected⟩ := captureOriginals_succeeds prepared valid ready weakReady
  obtain ⟨copies, copiesChecked, copiesProjected⟩ := captureCopies_succeeds prepared valid ready weakReady
  obtain ⟨external, externalChecked, externalProjected⟩ := captureUnits_succeeds heap state.copying units heap.initial.externalWeak
    (fun unit member => weakReady unit (List.mem_append_right _ member))
  let output : Format.HeapSnapshot := ⟨roots.toArray, (originals ++ copies).toArray, external, heap.initial.remembered⟩
  have projected : SnapshotProjection heap state units collection output := by
    refine ⟨?_, ⟨originals, copies, ?_, originalsProjected, copiesProjected⟩, externalProjected, rfl⟩
    · simpa only [output, List.toList_toArray] using rootsProjected
    · simp only [output]
  obtain ⟨sources, destinations, formed⟩ := projected.completed_wellFormed prepared valid ready strong weak
  have accepted := Format.validateHeap_completed_complete output sources destinations formed
  refine ⟨output, ?_, projected, sources, destinations, formed⟩
  simp [captureStrong, ready.complete, rootsChecked, originalsChecked, copiesChecked, externalChecked,
    accepted, output, bind, Except.bind, pure, Except.pure]

end

/-- The same core weak collector selected by the executable runner. -/
def evacuatedState (heap : PreparedHeap n k)
    (valid : Generational.Weak.WeakWellFormed ⟨heap.generational, heap.weak⟩) :
    Generational.CollectionKind → Copying.Weak.State n k
  | .full => Copying.Weak.collectWeak heap.copying heap.weak heap.roots
  | .young => (Generational.Weak.collectWeak (preparedWeakInput heap valid .young)).1

/-- All capture and coverage conditions follow from the selected core algorithm. -/
theorem evacuatedState_ready {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    (prepared : PreparedHeapProjection initial heap) (collection : Generational.CollectionKind) :
    let state := evacuatedState heap prepared.weakValid collection
    StrongCaptureReady heap state.strong collection ∧ state.pending = [] ∧ WeakCaptureReady heap state.strong.copying state.units ∧
      StrongCaptureCoverage heap state.strong ∧ WeakCaptureCoverage heap state.strong.copying state.units := by
  cases collection with
  | full =>
      obtain ⟨strong, done, weak⟩ := preparedFullCapture_ready prepared
      refine ⟨strong, done, weak, ?_, weakCaptureCoverage_full heap prepared.weakValid⟩
      change StrongCaptureCoverage heap (Copying.Weak.collectWeak heap.copying heap.weak heap.roots).strong
      rw [Copying.Weak.collectWeak_strong]
      exact strongCaptureCoverage_full heap
  | young =>
      obtain ⟨strong, done, weak⟩ := preparedGenerationalCapture_ready prepared .young
      refine ⟨strong, done, weak, ?_, weakCaptureCoverage_generational heap prepared.weakValid .young⟩
      let input := preparedWeakInput heap prepared.weakValid .young
      have same := congrArg Prod.fst (Generational.Weak.collectWeak_strong input)
      dsimp only at same
      change StrongCaptureCoverage heap (Generational.Weak.collectWeak input).1.strong
      rw [same]
      exact strongCaptureCoverage_generational heap .young

/-- Prepared evacuation succeeds in either mode and emits the selected core state exactly. -/
theorem collectPrepared_evacuate_complete {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    (prepared : PreparedHeapProjection initial heap) (valid : Format.FreshWellFormed initial)
    (collection : Generational.CollectionKind) :
    let state := evacuatedState heap prepared.weakValid collection
    ∃ output, collectPrepared heap .evacuate collection = .ok output ∧
      SnapshotProjection heap state.strong state.units collection output ∧
      ∃ originals copies, Format.CompletedWellFormed output originals copies := by
  obtain ⟨ready, done, weakReady, strong, weak⟩ := evacuatedState_ready prepared collection
  obtain ⟨output, captured, projected, formed⟩ := captureStrong_complete prepared valid ready weakReady strong weak
  refine ⟨output, ?_, projected, formed⟩
  cases collection with
  | full => exact captured
  | young =>
      have checked := validateWeak_complete (Generational.Weak.RawWeakHeap.mk heap.generational heap.weak) prepared.weakValid
      have youngMode : (Generational.CollectionKind.young == .young) = true := rfl
      have evacuateMode : (Algorithm.evacuate == .evacuate) = true := rfl
      simp [collectPrepared, checked, youngMode, evacuateMode, ← captured, ← done, evacuatedState, preparedWeakInput,
        bind, Except.bind]

/-- Every independently valid fresh heap evacuates successfully, with exact input,
    output and completed-format correspondence in both collection modes. -/
theorem runCollection_evacuate_complete (input : Format.HeapSnapshot)
    (valid : Format.FreshWellFormed (Format.orderedHeap input)) (collection : Generational.CollectionKind) :
    ∃ k, ∃ heap : PreparedHeap (Format.orderedHeap input).objects.size k, ∃ output,
      ∃ prepared : PreparedHeapProjection (Format.orderedHeap input) heap,
      runCollection input .evacuate collection = .ok output ∧
      SnapshotProjection heap (evacuatedState heap prepared.weakValid collection).strong
        (evacuatedState heap prepared.weakValid collection).units collection output ∧
      ∃ originals copies, Format.CompletedWellFormed output originals copies := by
  obtain ⟨k, heap, checked, prepared⟩ := prepareHeap_complete input valid
  obtain ⟨output, collected, projected, formed⟩ := collectPrepared_evacuate_complete prepared valid collection
  refine ⟨k, heap, output, prepared, ?_, projected, formed⟩
  simp [runCollection, checked, collected, bind, Except.bind]

/-- The CLI supports full marking and either evacuation mode. -/
def SupportedRun (algorithm : Algorithm) (collection : Generational.CollectionKind) : Prop :=
  collection = .full ∨ algorithm = .evacuate

/-- Every valid input succeeds for every documented algorithm/mode combination. -/
theorem runCollection_supported_complete (input : Format.HeapSnapshot)
    (valid : Format.FreshWellFormed (Format.orderedHeap input)) (algorithm : Algorithm)
    (collection : Generational.CollectionKind) (supported : SupportedRun algorithm collection) :
    ∃ output, runCollection input algorithm collection = .ok output := by
  cases algorithm with
  | mark =>
      cases collection with
      | full =>
          obtain ⟨_, _, output, _, collected, _⟩ := runCollection_mark_complete input valid
          exact ⟨output, collected⟩
      | young => simp [SupportedRun] at supported
  | evacuate =>
      obtain ⟨_, _, output, _, collected, _, _⟩ := runCollection_evacuate_complete input valid collection
      exact ⟨output, collected⟩

/-- Young marking is rejected before invoking either collector. -/
theorem collectPrepared_young_mark_rejected (heap : PreparedHeap n k) :
    collectPrepared heap .mark .young = .error "young collection supports evacuation only" := rfl

/-- Successful evacuation always produces a serializable completed snapshot. -/
theorem runCollection_evacuate_serializes (input : Format.HeapSnapshot)
    (valid : Format.FreshWellFormed (Format.orderedHeap input)) (collection : Generational.CollectionKind) :
    ∃ output bytes, runCollection input .evacuate collection = .ok output ∧ Format.writeHeap output = .ok bytes := by
  obtain ⟨_, _, output, _, collected, _, originals, copies, formed⟩ := runCollection_evacuate_complete input valid collection
  obtain ⟨bytes, serialized⟩ := Format.writeHeap_completed_complete output originals copies formed
  exact ⟨output, bytes, collected, serialized⟩

end CloverGC.Differential
