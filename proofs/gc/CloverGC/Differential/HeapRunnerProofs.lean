import CloverGC.Differential.HeapRunner
import CloverGC.Differential.HeapFormatProofs

namespace CloverGC.Differential

/-- The immutable part of an allocation's state: storage, generation and placement. -/
def Format.ObjectState.withoutHeader : Format.ObjectState → Format.ObjectState
  | .youngOriginal policy _ => .youngOriginal policy (.normal false)
  | .oldOriginal policy _ => .oldOriginal policy (.normal false)
  | .nonmovingOriginal _ => .nonmovingOriginal false
  | .ordinaryDestination => .ordinaryDestination
  | .nonmovingDestination => .nonmovingDestination

def Format.Allocation.identity (a : Format.Allocation) :
    Format.ObjectId × Format.AllocationId × Format.ObjectState :=
  (a.objectId, a.allocationId, a.state.withoutHeader)

/-- Physical reference encoding preserves source allocation identity and uses
    the global allocation ordinal for a copy. -/
def encodeReference (heap : PreparedHeap n k) : Copying.Reference n → Format.AllocationId
  | .source object => (heap.originals.get object).allocationId
  | .copied index => ⟨n + index⟩

/-- Tagged cell projection preserves immediate values and distinguishes null references. -/
def encodeCell (heap : PreparedHeap n k) :
    Copying.Weak.Cell (Copying.Reference n) → Format.Cell
  | .immediate value => .immediate value
  | .reference target => .reference (target.map (encodeReference heap))

def CellReferencesValid (copying : Copying.CopyState n) :
    Copying.Weak.Cell (Copying.Reference n) → Prop
  | .immediate _ | .reference none => True
  | .reference (some reference) => Copying.ValidReference copying reference

/-- Slot-by-slot projection preserves scalar cells and consumes exactly one
    valid model reference at each non-null reference position. -/
inductive CellsProjection (heap : PreparedHeap n k) (copying : Copying.CopyState n) :
    List Format.Cell → List (Copying.Reference n) → List Format.Cell → Prop where
  | nil : CellsProjection heap copying [] [] []
  | immediate (value : Int) (tail : CellsProjection heap copying layout refs output) :
      CellsProjection heap copying (.immediate value :: layout) refs (.immediate value :: output)
  | null (tail : CellsProjection heap copying layout refs output) :
      CellsProjection heap copying (.reference none :: layout) refs (.reference none :: output)
  | reference (old : Format.AllocationId) (reference : Copying.Reference n)
      (valid : Copying.ValidReference copying reference)
      (tail : CellsProjection heap copying layout refs output) :
      CellsProjection heap copying (.reference (some old) :: layout) (reference :: refs)
        (.reference (some (encodeReference heap reference)) :: output)

/-- The executable reference conversion succeeds exactly for allocated references. -/
theorem allocationOfReference_correct (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (reference : Copying.Reference n) (output : Format.AllocationId) :
    allocationOfReference heap copying reference = .ok output ↔
      Copying.ValidReference copying reference ∧ output = encodeReference heap reference := by
  cases reference with
  | source object =>
      simp [allocationOfReference, Copying.ValidReference, encodeReference, pure, Except.pure, eq_comm]
  | copied index =>
      cases allocated : copying.copies[index]? <;>
        simp [allocationOfReference, required, allocated, Copying.ValidReference,
          encodeReference, bind, Except.bind, pure, Except.pure, eq_comm]

private theorem mergeReferences_sound (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (layout : List Format.Cell) (refs : List (Copying.Reference n)) (output : List Format.Cell)
    (accepted : mergeReferences (allocationOfReference heap copying) layout refs = .ok output) :
    CellsProjection heap copying layout refs output := by
  induction layout generalizing refs output with
  | nil =>
      cases refs <;> simp [mergeReferences, pure, Except.pure] at accepted
      subst output
      exact .nil
  | cons cell layout ih =>
      cases cell with
      | immediate value =>
          cases rest : mergeReferences (allocationOfReference heap copying) layout refs with
          | error message => simp [mergeReferences, rest, bind, Except.bind] at accepted
          | ok tail =>
              simp [mergeReferences, rest, bind, Except.bind, pure, Except.pure] at accepted
              subst output
              exact .immediate value (ih refs tail rest)
      | reference target =>
          cases target with
          | none =>
              cases rest : mergeReferences (allocationOfReference heap copying) layout refs with
              | error message => simp [mergeReferences, rest, bind, Except.bind] at accepted
              | ok tail =>
                  simp [mergeReferences, rest, bind, Except.bind, pure, Except.pure] at accepted
                  subst output
                  exact .null (ih refs tail rest)
          | some old =>
              cases refs with
              | nil => simp [mergeReferences] at accepted
              | cons reference refs =>
                  cases converted : allocationOfReference heap copying reference with
                  | error message => simp [mergeReferences, converted, bind, Except.bind] at accepted
                  | ok id =>
                      cases rest : mergeReferences (allocationOfReference heap copying) layout refs with
                      | error message => simp [mergeReferences, converted, rest, bind, Except.bind] at accepted
                      | ok tail =>
                          simp [mergeReferences, converted, rest, bind, Except.bind, pure, Except.pure] at accepted
                          subst output
                          obtain ⟨valid, rfl⟩ := (allocationOfReference_correct heap copying reference id).mp converted
                          exact .reference old reference valid (ih refs tail rest)

private theorem mergeReferences_complete (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (projected : CellsProjection heap copying layout refs output) :
    mergeReferences (allocationOfReference heap copying) layout refs = .ok output := by
  induction projected with
  | nil => rfl
  | immediate value tail ih => simp [mergeReferences, ih, bind, Except.bind, pure, Except.pure]
  | null tail ih => simp [mergeReferences, ih, bind, Except.bind, pure, Except.pure]
  | reference old reference valid tail ih =>
      have converted := (allocationOfReference_correct heap copying reference
        (encodeReference heap reference)).mpr ⟨valid, rfl⟩
      simp [mergeReferences, converted, ih, bind, Except.bind, pure, Except.pure]

/-- Ordered merging succeeds exactly when every cell has the specified projection. -/
theorem mergeReferences_correct (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (layout : List Format.Cell) (refs : List (Copying.Reference n)) (output : List Format.Cell) :
    mergeReferences (allocationOfReference heap copying) layout refs = .ok output ↔
      CellsProjection heap copying layout refs output :=
  ⟨mergeReferences_sound heap copying layout refs output, mergeReferences_complete heap copying⟩

theorem CellsProjection.length (projected : CellsProjection heap copying layout refs output) :
    output.length = layout.length := by
  induction projected <;> simp_all

/-- Cell capture preserves its tag, value and physical reference encoding. -/
theorem captureCell_correct (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (cell : Copying.Weak.Cell (Copying.Reference n)) (output : Format.Cell) :
    captureCell (allocationOfReference heap copying) cell = .ok output ↔
      CellReferencesValid copying cell ∧ output = encodeCell heap cell := by
  cases cell with
  | immediate value =>
      simp [captureCell, CellReferencesValid, encodeCell, pure, Except.pure, eq_comm]
  | reference target =>
      cases target with
      | none =>
          simp [captureCell, CellReferencesValid, encodeCell, Option.mapM,
            bind, Except.bind, pure, Except.pure, eq_comm]
      | some reference =>
          cases converted : allocationOfReference heap copying reference with
          | error message =>
              have invalid := allocationOfReference_correct heap copying reference
                (encodeReference heap reference)
              simp [converted] at invalid
              simp [captureCell, CellReferencesValid, invalid, Option.mapM, converted,
                bind, Except.bind, Functor.map, Except.map]
          | ok id =>
              obtain ⟨valid, rfl⟩ := (allocationOfReference_correct heap copying reference id).mp converted
              simp [captureCell, CellReferencesValid, valid, encodeCell, Option.mapM, converted,
                bind, Except.bind, Functor.map, Except.map, pure, Except.pure, eq_comm]

/-- Valid cells project successfully, including immediate zero and null references. -/
theorem captureCell_succeeds (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (cell : Copying.Weak.Cell (Copying.Reference n)) (valid : CellReferencesValid copying cell) :
    captureCell (allocationOfReference heap copying) cell = .ok (encodeCell heap cell) :=
  (captureCell_correct heap copying cell _).mpr ⟨valid, rfl⟩

private theorem mapM_observe (f : α → Except String β) (before : α → γ) (after : β → γ)
    (preserves : ∀ a b, f a = .ok b → after b = before a)
    (xs : List α) (ys : List β) (accepted : xs.mapM f = .ok ys) :
    ys.map after = xs.map before := by
  induction xs generalizing ys with
  | nil =>
      simp [pure, Except.pure] at accepted
      subst ys
      rfl
  | cons x xs ih =>
      cases first : f x with
      | error message => simp [List.mapM_cons, first, bind, Except.bind] at accepted
      | ok y =>
          cases rest : xs.mapM f with
          | error message => simp [List.mapM_cons, first, rest, bind, Except.bind] at accepted
          | ok tail =>
              simp [List.mapM_cons, first, rest, bind, Except.bind, pure, Except.pure] at accepted
              subst ys
              simp only [List.map_cons, preserves x y first, ih tail rest]

private theorem originalState_identity (initial : Format.ObjectState) (header : Format.Header)
    (result : Format.ObjectState) (accepted : originalState initial header = .ok result) :
    result.withoutHeader = initial.withoutHeader := by
  cases initial <;> cases header <;>
    simp [originalState, pure, Except.pure] at accepted <;> cases accepted <;> rfl

private theorem captureOriginal_identity (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (initial result : Format.Allocation)
    (accepted : captureOriginal heap state units collection initial = .ok result) :
    result.identity = initial.identity := by
  unfold captureOriginal at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    simp only [Format.Allocation.identity, Prod.mk.injEq, true_and]
    apply originalState_identity
    assumption

private def copyIdentity (n : Nat) (record : Copying.CopyRecord n × Nat) :
    Format.ObjectId × Format.AllocationId × Format.ObjectState :=
  (⟨s!"obj-{record.1.origin.val}"⟩, ⟨n + record.2⟩,
    match record.1.destination with
    | .ordinary => .ordinaryDestination
    | .nonMoving => .nonmovingDestination)

private theorem captureCopy_identity (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (record : Copying.CopyRecord n) (index : Nat) (result : Format.Allocation)
    (accepted : captureCopy heap state units collection record index = .ok result) :
    result.identity = copyIdentity n (record, index) := by
  unfold captureCopy at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    simp_all [copyIdentity, Format.Allocation.identity, Format.ObjectState.withoutHeader]

private theorem captureStrong_identities (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (output : Format.HeapSnapshot)
    (accepted : captureStrong heap state units collection = .ok output) :
    output.objects.toList.map Format.Allocation.identity =
      heap.initial.objects.toList.map Format.Allocation.identity ++
        state.copying.copies.zipIdx.map (copyIdentity n) := by
  unfold captureStrong at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    simp only [List.map_append]
    congr 1
    · exact mapM_observe _ Format.Allocation.identity Format.Allocation.identity
        (captureOriginal_identity heap state units collection) heap.initial.objects.toList _ (by assumption)
    · exact mapM_observe _ (copyIdentity n) Format.Allocation.identity
        (fun pair result h => captureCopy_identity heap state units collection pair.1 pair.2 result h)
        state.copying.copies.zipIdx _ (by assumption)

/-- Mark capture preserves the complete original allocation identity sequence. -/
theorem captureMark_identities (heap : PreparedHeap n k) (state : MarkState n)
    (output : Format.HeapSnapshot) (accepted : captureMark heap state = .ok output) :
    output.objects.toList.map Format.Allocation.identity =
      heap.initial.objects.toList.map Format.Allocation.identity := by
  unfold captureMark at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    apply mapM_observe (captureMarkedOriginal heap state) Format.Allocation.identity Format.Allocation.identity
      ?_ heap.initial.objects.toList _ (by assumption)
    intro initial result captured
    unfold captureMarkedOriginal at captured
    simp only [bind, Except.bind, pure, Except.pure] at captured
    repeat' first | contradiction | split at captured
    all_goals
      cases captured
      simp only [Format.Allocation.identity, Prod.mk.injEq, true_and]
      apply originalState_identity
      assumption

/-- Full evacuation retains every original, followed by exactly the allocated copies. -/
theorem captureEvacuated_identities (heap : PreparedHeap n k) (state : Copying.Weak.State n k)
    (output : Format.HeapSnapshot) (accepted : captureEvacuated heap state = .ok output) :
    output.objects.toList.map Format.Allocation.identity =
      heap.initial.objects.toList.map Format.Allocation.identity ++
        state.strong.copying.copies.zipIdx.map (copyIdentity n) := by
  unfold captureEvacuated at accepted
  simp only [bind, Except.bind] at accepted
  split at accepted
  · exact captureStrong_identities heap state.strong state.units .full output accepted
  · contradiction

/-- Copy allocation IDs remain the global allocation ordinals following all originals. -/
theorem captureEvacuated_allocation_ids (heap : PreparedHeap n k) (state : Copying.Weak.State n k)
    (output : Format.HeapSnapshot) (accepted : captureEvacuated heap state = .ok output) :
    output.objects.toList.map Format.Allocation.allocationId =
      heap.initial.objects.toList.map Format.Allocation.allocationId ++
        state.strong.copying.copies.zipIdx.map (fun pair => (⟨n + pair.2⟩ : Format.AllocationId)) := by
  have identities := congrArg (List.map (fun identity => identity.2.1))
    (captureEvacuated_identities heap state output accepted)
  simpa [List.map_append, List.map_map, Function.comp_def, Format.Allocation.identity,
    copyIdentity] using identities

/-- Preparation retains precisely the ordered fresh snapshot, not a reconstructed subset. -/
theorem prepareHeap_initial (input : Format.HeapSnapshot) (n k : Nat) (heap : PreparedHeap n k)
    (accepted : prepareHeap input = .ok ⟨n, k, heap⟩) :
    heap.initial.objects.toList.Perm input.objects.toList := by
  unfold prepareHeap at accepted
  cases fresh : Format.prepareFreshHeap input with
  | error message => simp [fresh, bind, Except.bind] at accepted
  | ok initial =>
      cases model : prepareModel initial with
      | error message => simp [fresh, model, bind, Except.bind] at accepted
      | ok prepared =>
          rcases prepared with ⟨units, checked⟩
          simp only [fresh, model, bind, Except.bind, pure, Except.pure] at accepted
          cases accepted
          rw [checked.property]
          exact Format.prepareFreshHeap_objects_perm input initial fresh

private theorem collectPrepared_identities (heap : PreparedHeap n k) (algorithm : Algorithm)
    (collection : Generational.CollectionKind) (output : Format.HeapSnapshot)
    (accepted : collectPrepared heap algorithm collection = .ok output) :
    ∃ copies, output.objects.toList.map Format.Allocation.identity =
      heap.initial.objects.toList.map Format.Allocation.identity ++ copies := by
  unfold collectPrepared at accepted
  simp only [bind, Except.bind] at accepted
  repeat' first | contradiction | split at accepted
  all_goals first
    | exact ⟨_, captureStrong_identities _ _ _ _ _ accepted⟩
    | exact ⟨_, captureEvacuated_identities _ _ _ accepted⟩
    | exact ⟨[], by simpa using captureMark_identities _ _ _ accepted⟩

/-- The returned heap starts with every input original, retaining identity,
    generation, storage and placement. Input order may be normalized by allocation ID. -/
theorem runCollection_original_prefix (input output : Format.HeapSnapshot) (algorithm : Algorithm)
    (collection : Generational.CollectionKind)
    (accepted : runCollection input algorithm collection = .ok output) :
    ((output.objects.toList.take input.objects.size).map Format.Allocation.identity).Perm
      (input.objects.toList.map Format.Allocation.identity) := by
  unfold runCollection at accepted
  cases prepared : prepareHeap input with
  | error message => simp [prepared, bind, Except.bind] at accepted
  | ok heap =>
      rcases heap with ⟨n, k, heap⟩
      simp only [prepared, bind, Except.bind] at accepted
      obtain ⟨copies, captured⟩ := collectPrepared_identities heap algorithm collection output accepted
      have originals := prepareHeap_initial input n k heap prepared
      have length := originals.length_eq
      rw [List.map_take, captured]
      have count : input.objects.size = (heap.initial.objects.toList.map Format.Allocation.identity).length := by
        simpa using length.symm
      rw [count]
      simpa using originals.map Format.Allocation.identity

/-- In particular no old original, even an unreachable one in a young collection,
    can disappear or change physical allocation or generation in successful output. -/
theorem runCollection_preserves_original (input output : Format.HeapSnapshot) (algorithm : Algorithm)
    (collection : Generational.CollectionKind)
    (accepted : runCollection input algorithm collection = .ok output)
    (original : Format.Allocation) (member : original ∈ input.objects.toList) :
    ∃ allocation ∈ output.objects.toList, allocation.identity = original.identity := by
  have originalPrefix := runCollection_original_prefix input output algorithm collection accepted
  have present := originalPrefix.mem_iff.mpr (List.mem_map.mpr ⟨original, member, rfl⟩)
  obtain ⟨allocation, inside, same⟩ := List.mem_map.mp present
  exact ⟨allocation, List.mem_of_mem_take inside, same⟩

/-- Original strong slots expose the fields of the corresponding model object. -/
theorem captureOriginal_strong (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (initial output : Format.Allocation)
    (accepted : captureOriginal heap state units collection initial = .ok output) :
    ∃ object, originalReference heap.objectOfAllocation initial.allocationId = .ok object ∧
      CellsProjection heap state.copying initial.strong.toList
        (state.originalFields object) output.strong.toList := by
  unfold captureOriginal at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    refine ⟨_, by assumption, ?_⟩
    exact (mergeReferences_correct _ _ _ _ _).mp (by assumption)

/-- Destination strong slots expose their copy record's repaired fields. -/
theorem captureCopy_strong (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (record : Copying.CopyRecord n) (index : Nat) (output : Format.Allocation)
    (accepted : captureCopy heap state units collection record index = .ok output) :
    CellsProjection heap state.copying (heap.originals.get record.origin).strong.toList
      record.strongChildren output.strong.toList := by
  unfold captureCopy at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    exact (mergeReferences_correct _ _ _ _ _).mp (by assumption)

/-- The emitted root array preserves every cell and the model's ordered references. -/
theorem captureStrong_roots (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (output : Format.HeapSnapshot)
    (accepted : captureStrong heap state units collection = .ok output) :
    CellsProjection heap state.copying heap.initial.roots.toList state.roots output.roots.toList := by
  unfold captureStrong at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    exact (mergeReferences_correct _ _ _ _ _).mp (by assumption)

theorem captureEvacuated_roots (heap : PreparedHeap n k) (state : Copying.Weak.State n k)
    (output : Format.HeapSnapshot) (accepted : captureEvacuated heap state = .ok output) :
    CellsProjection heap state.strong.copying heap.initial.roots.toList
      state.strong.roots output.roots.toList := by
  unfold captureEvacuated at accepted
  simp only [bind, Except.bind] at accepted
  split at accepted
  · exact captureStrong_roots heap state.strong state.units .full output accepted
  · contradiction

/-- Mark capture leaves the root cells in their original order. -/
theorem captureMark_roots (heap : PreparedHeap n k) (state : MarkState n)
    (output : Format.HeapSnapshot) (accepted : captureMark heap state = .ok output) :
    output.roots = heap.initial.roots := by
  unfold captureMark at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals cases accepted; rfl

/-- Marking changes headers while leaving every original strong slot unchanged. -/
theorem captureMarkedOriginal_strong (heap : PreparedHeap n k) (state : MarkState n)
    (initial output : Format.Allocation)
    (accepted : captureMarkedOriginal heap state initial = .ok output) :
    output.strong = initial.strong := by
  unfold captureMarkedOriginal at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals cases accepted; rfl

end CloverGC.Differential
