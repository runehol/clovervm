import CloverGC.Differential.HeapRunner
import CloverGC.EvacuationProofs
import CloverGC.GenerationalCorrectness

namespace CloverGC.Differential

/-- Source-age condition for avoiding diagnostic young source storage. -/
def NonYoungReference (heap : PreparedHeap n k) : Copying.Reference n → Prop
  | .source object => heap.generational.data.isYoung object = false
  | .copied _ => True

/-- Reference cardinality and allocated destinations needed by slot capture. -/
def FieldsReady (copying : Copying.CopyState n) (refs : List (Copying.Reference n)) (count : Nat) : Prop :=
  refs.length = count ∧ ∀ reference ∈ refs, Copying.ValidReference copying reference

private theorem canonical_allocated {input : Copying.CopyInput n} {copying : Copying.CopyState n} (ledger : Copying.Ledger input copying) (object : Fin n) :
    Copying.ValidReference copying (Copying.canonical copying object) := by
  cases forward : copying.forwarding object with
  | none => simp [Copying.canonical, forward, Copying.ValidReference]
  | some index =>
      obtain ⟨record, allocated, _⟩ := ledger.forwarding_valid object index forward
      simpa only [Copying.canonical, forward, Copying.ValidReference] using (show ∃ record, copying.copies[index]? = some record from ⟨record, allocated⟩)

private theorem mappedFields_ready (copying : Copying.CopyState n) (objects : List (Fin n))
    (convert : Fin n → Copying.Reference n) (safe : ∀ object, Copying.ValidReference copying (convert object)) :
    FieldsReady copying (objects.map convert) objects.length := by
  refine ⟨by simp, ?_⟩
  intro reference member
  obtain ⟨object, _, rfl⟩ := List.mem_map.mp member
  exact safe object

private theorem rawFields_ready (input : Copying.CopyInput n) (copying : Copying.CopyState n) (object : Fin n) :
    FieldsReady copying (Copying.rawFields input object) (input.heap.strongChildren object).length :=
  mappedFields_ready copying _ .source (fun _ => trivial)

private theorem fieldPlan_ready (input : Copying.CopyInput n) (copying : Copying.CopyState n)
    (ledger : Copying.Ledger input copying) (scanned : List (Fin n)) (object : Fin n) :
    FieldsReady copying (Copying.fieldPlan input copying scanned object) (input.heap.strongChildren object).length := by
  unfold Copying.fieldPlan
  split
  · exact mappedFields_ready copying _ _ (canonical_allocated ledger)
  · exact rawFields_ready input copying object

private theorem originalPlan_ready (input : Generational.CollectionInput n) (copying : Copying.CopyState n)
    (ledger : Copying.Ledger input.copyInput copying) (done scanned : List (Fin n)) (object : Fin n) :
    FieldsReady copying (Generational.originalPlan input copying done scanned object)
      (input.heap.data.graph.strongChildren object).length := by
  unfold Generational.originalPlan
  split
  · exact mappedFields_ready copying _ _ (canonical_allocated ledger)
  · exact rawFields_ready input.copyInput copying object

private theorem copyPlan_ready (input : Generational.CollectionInput n) (copying : Copying.CopyState n)
    (ledger : Copying.Ledger input.copyInput copying) (scanned : List (Fin n)) (object : Fin n) :
    FieldsReady copying (Generational.copyPlan input copying scanned object)
      (input.heap.data.graph.strongChildren object).length := by
  unfold Generational.copyPlan
  split
  · exact mappedFields_ready copying _ _ (canonical_allocated ledger)
  · exact rawFields_ready input.copyInput copying object

/-- The semantic core state conditions required by strong snapshot capture. -/
structure StrongCaptureReady (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (collection : Generational.CollectionKind) : Prop where
  copying : Copying.CopyInvariant heap.copying state.copying
  complete : state.copying.pending = []
  roots : FieldsReady state.copying state.roots heap.roots.length
  originals : ∀ object, FieldsReady state.copying (state.originalFields object)
    (heap.copying.heap.strongChildren object).length
  copies : ∀ (index : Nat) (record : Copying.CopyRecord n), state.copying.copies[index]? = some record →
    FieldsReady state.copying record.strongChildren (heap.copying.heap.strongChildren record.origin).length
  unselected : ∀ object, (heap.collectionInput collection).selected object = false →
    state.copying.forwarding object = none ∧ .source object ∉ state.copying.marked

/-- Every allocated copy forwards its origin, while unselected originals have
    no forwarding destination. Copy selection follows from those two facts. -/
theorem StrongCaptureReady.selected {heap : PreparedHeap n k} {state : Copying.EvacuationState n}
    {collection : Generational.CollectionKind} (ready : StrongCaptureReady heap state collection)
    (index : Nat) (record : Copying.CopyRecord n) (allocated : state.copying.copies[index]? = some record) :
    (heap.collectionInput collection).selected record.origin = true := by
  have forward := ready.copying.ledger.copies_forwarded index record allocated
  cases selected : (heap.collectionInput collection).selected record.origin with
  | true => rfl
  | false => rw [(ready.unselected record.origin selected).1] at forward; contradiction

/-- Full evacuation supplies all strong capture conditions directly from its core invariant. -/
theorem strongCaptureReady_full (heap : PreparedHeap n k) :
    StrongCaptureReady heap (Copying.evacuate heap.copying heap.roots).1 .full := by
  have invariant := Copying.evacuate_invariant heap.copying heap.roots
  refine ⟨invariant.copying, Copying.evacuate_complete _ _, ?_, ?_, ?_, ?_⟩
  · rw [invariant.roots_repaired]
    exact mappedFields_ready _ _ _ (canonical_allocated invariant.copying.ledger)
  · intro object
    rw [invariant.fields.originals object]
    split
    · exact fieldPlan_ready _ _ invariant.copying.ledger _ object
    · exact rawFields_ready _ _ object
  · intro index record allocated
    rw [invariant.fields.copies index record allocated]
    exact fieldPlan_ready _ _ invariant.copying.ledger _ record.origin
  · intro object outside
    simp [PreparedHeap.collectionInput, Generational.CollectionInput.selected] at outside

/-- Generational evacuation supplies the same conditions, including old-object retention. -/
theorem strongCaptureReady_generational (heap : PreparedHeap n k) (collection : Generational.CollectionKind) :
    StrongCaptureReady heap (Generational.evacuate (heap.collectionInput collection)).1 collection := by
  have invariant := Generational.evacuate_invariant (heap.collectionInput collection)
  refine ⟨invariant.tracking.copying, Generational.evacuate_complete _, ?_, ?_, ?_, ?_⟩
  · rw [invariant.roots_repaired]
    exact mappedFields_ready _ _ _ (canonical_allocated invariant.tracking.copying.ledger)
  · intro object
    rw [invariant.fields.originals object]
    exact originalPlan_ready _ _ invariant.tracking.copying.ledger _ _ object
  · intro index record allocated
    rw [invariant.fields.copies index record allocated]
    exact copyPlan_ready _ _ invariant.tracking.copying.ledger _ record.origin
  · exact Generational.evacuate_unselected (heap.collectionInput collection)

end CloverGC.Differential
