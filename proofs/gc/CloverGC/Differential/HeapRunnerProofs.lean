import CloverGC.Differential.HeapRunner
import CloverGC.Differential.HeapFormatProofs

namespace CloverGC.Differential

/-- An ordered, length-preserving relation; no allocation can be dropped or duplicated. -/
inductive ListProjection (relation : α → β → Prop) : List α → List β → Prop where
  | nil : ListProjection relation [] []
  | cons : relation a b → ListProjection relation rest output →
      ListProjection relation (a :: rest) (b :: output)

theorem ListProjection.before_exists (projected : ListProjection relation before after)
    (member : a ∈ before) : ∃ b ∈ after, relation a b := by
  induction projected generalizing a with
  | nil => cases member
  | cons head tail ih =>
      rcases List.mem_cons.mp member with same | inside
      · subst a; exact ⟨_, by simp, head⟩
      · obtain ⟨b, inside, related⟩ := ih inside
        exact ⟨b, by simp [inside], related⟩

theorem ListProjection.after_exists (projected : ListProjection relation before after)
    (member : b ∈ after) : ∃ a ∈ before, relation a b := by
  induction projected generalizing b with
  | nil => cases member
  | cons head tail ih =>
      rcases List.mem_cons.mp member with same | inside
      · subst b; exact ⟨_, by simp, head⟩
      · obtain ⟨a, inside, related⟩ := ih inside
        exact ⟨a, by simp [inside], related⟩

/-- Ordered projection preserves cardinality. -/
theorem ListProjection.length (projected : ListProjection relation before after) : after.length = before.length := by
  induction projected <;> simp_all

/-- Any pointwise preserved property remains equal in array order. -/
theorem ListProjection.map_eq {relation : α → β → Prop} {before : List α} {after : List β} (projected : ListProjection relation before after)
    (left : α → γ) (right : β → γ) (same : ∀ a b, relation a b → right b = left a) :
    after.map right = before.map left := by
  induction projected with
  | nil => rfl
  | cons head tail ih => simp [same _ _ head, ih]

/-- Ordered projection preserves a concatenated pointwise property. -/
theorem ListProjection.flatMap_eq {relation : α → β → Prop} {before : List α} {after : List β}
    (projected : ListProjection relation before after) (left : α → List γ) (right : β → List γ)
    (same : ∀ a b, relation a b → right b = left a) : after.flatMap right = before.flatMap left := by
  induction projected with
  | nil => rfl
  | cons head tail ih => simp [same _ _ head, ih]

theorem ListProjection.append (left : ListProjection relation beforeLeft afterLeft)
    (right : ListProjection relation beforeRight afterRight) :
    ListProjection relation (beforeLeft ++ beforeRight) (afterLeft ++ afterRight) := by
  induction left with
  | nil => exact right
  | cons head _ ih => exact .cons head ih

/-- Ordered projection relates corresponding positions, not only memberships. -/
theorem ListProjection.getElem (projected : ListProjection relation before after) (i : Nat)
    (insideBefore : i < before.length) (insideAfter : i < after.length) :
    relation before[i] after[i] := by
  induction projected generalizing i with
  | nil => simp at insideBefore
  | cons head _ ih =>
      cases i with
      | zero => exact head
      | succ i => exact ih i (by simpa using insideBefore) (by simpa using insideAfter)

private theorem mapM_projects (f : α → Except String β) (relation : α → β → Prop)
    (projects : ∀ a b, f a = .ok b → relation a b)
    (xs : List α) (ys : List β) (accepted : xs.mapM f = .ok ys) :
    ListProjection relation xs ys := by
  induction xs generalizing ys with
  | nil =>
      simp [pure, Except.pure] at accepted
      subst ys
      exact .nil
  | cons x xs ih =>
      cases first : f x with
      | error message => simp [List.mapM_cons, first, bind, Except.bind] at accepted
      | ok y =>
          cases rest : xs.mapM f with
          | error message => simp [List.mapM_cons, first, rest, bind, Except.bind] at accepted
          | ok tail =>
              simp [List.mapM_cons, first, rest, bind, Except.bind, pure, Except.pure] at accepted
              subst ys
              exact .cons (projects x y first) (ih tail rest)

private theorem mapM_complete (f : α → Except String β) (relation : α → β → Prop)
    (complete : ∀ a b, relation a b → f a = .ok b)
    (projected : ListProjection relation xs ys) : xs.mapM f = .ok ys := by
  induction projected with
  | nil => rfl
  | cons head tail ih =>
      simp [List.mapM_cons, complete _ _ head, ih, bind, Except.bind, pure, Except.pure]

private theorem mapM_correct (f : α → Except String β) (relation : α → β → Prop)
    (correct : ∀ a b, f a = .ok b ↔ relation a b) (xs : List α) (ys : List β) :
    xs.mapM f = .ok ys ↔ ListProjection relation xs ys :=
  ⟨mapM_projects f relation (fun a b => (correct a b).mp) xs ys,
    mapM_complete f relation (fun a b => (correct a b).mpr)⟩

theorem mapM_success (values : List α) (f : α → Except String β) (relation : α → β → Prop)
    (each : ∀ a ∈ values, ∃ b, f a = .ok b ∧ relation a b) :
    ∃ output, values.mapM f = .ok output ∧ ListProjection relation values output := by
  induction values with
  | nil => exact ⟨[], rfl, .nil⟩
  | cons a rest ih =>
      obtain ⟨b, checked, projects⟩ := each a (by simp)
      obtain ⟨tail, tailChecked, tailProjects⟩ := ih (fun a member => each a (by simp [member]))
      exact ⟨b :: tail, by simp [List.mapM_cons, checked, tailChecked, bind, Except.bind, pure, Except.pure],
        .cons projects tailProjects⟩

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
  | immediate (value : Int64) (tail : CellsProjection heap copying layout refs output) :
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

private def copyIdentity (heap : PreparedHeap n k) (record : Copying.CopyRecord n × Nat) :
    Format.ObjectId × Format.AllocationId × Format.ObjectState :=
  ((heap.originals.get record.1.origin).objectId, ⟨n + record.2⟩,
    match record.1.destination with
    | .ordinary => .ordinaryDestination
    | .nonMoving => .nonmovingDestination)

private theorem captureCopy_identity (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (record : Copying.CopyRecord n) (index : Nat) (result : Format.Allocation)
    (accepted : captureCopy heap state units collection record index = .ok result) :
    result.identity = copyIdentity heap (record, index) := by
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
        state.copying.copies.zipIdx.map (copyIdentity heap) := by
  unfold captureStrong at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    simp only [List.map_append]
    congr 1
    · exact mapM_observe _ Format.Allocation.identity Format.Allocation.identity
        (captureOriginal_identity heap state units collection) heap.initial.objects.toList _ (by assumption)
    · exact mapM_observe _ (copyIdentity heap) Format.Allocation.identity
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
        state.strong.copying.copies.zipIdx.map (copyIdentity heap) := by
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

/-- Encoding observes the supplied unit, including inactive entries. -/
def encodeUnit (heap : PreparedHeap n k) : Copying.Weak.Unit n → Format.WeakContents
  | .field cell => .field (encodeCell heap cell)
  | .entry e => .entry ⟨e.firstGuard.map (encodeReference heap),
      e.secondGuard.map (encodeReference heap), encodeCell heap e.payload⟩

def GuardReferencesValid (copying : Copying.CopyState n) (guard : Option (Copying.Reference n)) : Prop :=
  ∀ reference ∈ guard, Copying.ValidReference copying reference

def UnitReferencesValid (copying : Copying.CopyState n) : Copying.Weak.Unit n → Prop
  | .field cell => CellReferencesValid copying cell
  | .entry e => GuardReferencesValid copying e.firstGuard ∧
      GuardReferencesValid copying e.secondGuard ∧ CellReferencesValid copying e.payload

def UnitKindMatches : Format.WeakContents → Copying.Weak.Unit n → Prop
  | .field _, .field _ | .entry _, .entry _ => True
  | _, _ => False

/-- The physical layout supplies the logical unit ID and kind; contents come
    from the actual model unit without additional weak repair. -/
def UnitProjection (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout output : Format.WeakUnit) : Prop :=
  ∃ id : Fin k, id.val = layout.unitId.value ∧ UnitKindMatches layout.contents (units id) ∧
    UnitReferencesValid copying (units id) ∧ output = ⟨layout.unitId, encodeUnit heap (units id)⟩

def UnitsProjection (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout output : Array Format.WeakUnit) : Prop :=
  ListProjection (UnitProjection heap copying units) layout.toList output.toList

private theorem checkedFin_correct (value limit : Nat) (id : Fin limit) :
    checkedFin value limit = .ok id ↔ id.val = value := by
  by_cases bound : value < limit
  · simp [checkedFin, bound, pure, Except.pure, Fin.ext_iff, eq_comm]
  · simp only [checkedFin, dite_eq_right bound, reduceCtorEq, false_iff]
    intro same
    exact bound (same ▸ id.isLt)

private theorem captureGuard_correct (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (guard : Option (Copying.Reference n)) (output : Format.Reference) :
    guard.mapM (allocationOfReference heap copying) = .ok output ↔
      GuardReferencesValid copying guard ∧ output = guard.map (encodeReference heap) := by
  cases guard with
  | none => simp [Option.mapM, GuardReferencesValid, pure, Except.pure, eq_comm]
  | some reference =>
      have cell := captureCell_correct heap copying (.reference (some reference)) (.reference output)
      cases converted : allocationOfReference heap copying reference <;>
        simpa [captureCell, CellReferencesValid, GuardReferencesValid, encodeCell,
          Option.mapM, converted, bind, Except.bind, Functor.map, Except.map,
          pure, Except.pure] using cell

private theorem captureUnit_sound (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout output : Format.WeakUnit)
    (accepted : captureUnit heap copying units layout = .ok output) :
    UnitProjection heap copying units layout output := by
  unfold captureUnit at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    refine ⟨_, (checkedFin_correct _ _ _).mp (by assumption), ?_⟩
  all_goals
    have payload := (captureCell_correct heap copying _ _).mp (by assumption)
    simp_all [UnitKindMatches, UnitReferencesValid, encodeUnit]
  all_goals
    repeat' constructor
    all_goals first
      | exact ((captureGuard_correct heap copying _ _).mp (by assumption)).1
      | exact ((captureGuard_correct heap copying _ _).mp (by assumption)).2

private theorem captureUnit_complete (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout output : Format.WeakUnit)
    (projected : UnitProjection heap copying units layout output) :
    captureUnit heap copying units layout = .ok output := by
  obtain ⟨id, lookup, kind, valid, rfl⟩ := projected
  have checked := (checkedFin_correct layout.unitId.value k id).mpr lookup
  cases shape : layout.contents with
  | field old =>
      cases model : units id with
      | field cell =>
          have captured := captureCell_succeeds heap copying cell
            (by simpa [model, UnitReferencesValid] using valid)
          simp [captureUnit, checked, shape, model, captured, encodeUnit, bind, Except.bind, pure, Except.pure]
      | entry e => simp [shape, model, UnitKindMatches] at kind
  | entry old =>
      cases model : units id with
      | field cell => simp [shape, model, UnitKindMatches] at kind
      | entry e =>
          simp only [model, UnitReferencesValid] at valid
          have first := (captureGuard_correct heap copying e.firstGuard _).mpr ⟨valid.1, rfl⟩
          have second := (captureGuard_correct heap copying e.secondGuard _).mpr ⟨valid.2.1, rfl⟩
          have payload := captureCell_succeeds heap copying e.payload valid.2.2
          simp [captureUnit, checked, shape, model, first, second, payload,
            encodeUnit, bind, Except.bind, pure, Except.pure]

theorem captureUnit_correct (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout output : Format.WeakUnit) :
    captureUnit heap copying units layout = .ok output ↔ UnitProjection heap copying units layout output :=
  ⟨captureUnit_sound heap copying units layout output, captureUnit_complete heap copying units layout output⟩

/-- Capture preserves unit order, IDs, kinds, and every observed guard and payload. -/
theorem captureUnits_correct (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout output : Array Format.WeakUnit) :
    captureUnits heap copying units layout = .ok output ↔ UnitsProjection heap copying units layout output := by
  have relation := mapM_correct (captureUnit heap copying units) (UnitProjection heap copying units)
    (captureUnit_correct heap copying units) layout.toList output.toList
  unfold captureUnits UnitsProjection
  rw [Array.mapM_eq_mapM_toList, ← relation]
  cases layout.toList.mapM (captureUnit heap copying units) <;>
    simp [Functor.map, Except.map, Array.toArray_eq]

theorem UnitsProjection.size (projected : UnitsProjection heap copying units layout output) :
    output.size = layout.size := by
  simpa using ListProjection.length projected

theorem UnitsProjection.ids (projected : UnitsProjection heap copying units layout output) :
    output.toList.map Format.WeakUnit.unitId = layout.toList.map Format.WeakUnit.unitId := by
  apply ListProjection.map_eq projected
  rintro _ _ ⟨_, _, _, _, rfl⟩
  rfl

/-- Headers encode the observed forwarding table, or the source mark when unforwarded. -/
def encodeHeader (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (object : Fin n) : Format.Header :=
  match copying.forwarding object with
  | none => .normal (decide (.source object ∈ copying.marked))
  | some index => .forwarded (encodeReference heap (.copied index))

/-- Generation and placement stay attached to an original while its header changes. -/
inductive OriginalStateProjection : Format.ObjectState → Format.Header → Format.ObjectState → Prop where
  | young : OriginalStateProjection (.youngOriginal policy old) header (.youngOriginal policy header)
  | old : OriginalStateProjection (.oldOriginal policy old) header (.oldOriginal policy header)
  | nonmoving : OriginalStateProjection (.nonmovingOriginal old) (.normal marked) (.nonmovingOriginal marked)

/-- Physical original IDs select the corresponding logical object through the retained pairing. -/
def OriginalObject (heap : PreparedHeap n k) (initial : Format.Allocation) (object : Fin n) : Prop :=
  ∃ index : Fin n, index.val = initial.allocationId.value ∧ heap.objectOfAllocation.get index = object

structure OriginalProjection (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (initial output : Format.Allocation) : Prop where
  object : ∃ object, OriginalObject heap initial object ∧
    OriginalStateProjection initial.state (encodeHeader heap state.copying object) output.state ∧
    CellsProjection heap state.copying initial.strong.toList (state.originalFields object) output.strong.toList ∧
    (if (heap.collectionInput collection).selected object && heap.copying.movable object then
      output.weak = initial.weak else UnitsProjection heap state.copying units initial.weak output.weak)
  objectId : output.objectId = initial.objectId
  allocationId : output.allocationId = initial.allocationId

structure CopyProjection (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (pair : Copying.CopyRecord n × Nat)
    (output : Format.Allocation) : Prop where
  identity : output.identity = copyIdentity heap pair
  strong : CellsProjection heap state.copying (heap.originals.get pair.1.origin).strong.toList
    pair.1.strongChildren output.strong.toList
  weak : UnitsProjection heap state.copying units (heap.originals.get pair.1.origin).weak output.weak

/-- Complete physical evacuation output, independently specified from capture control flow. -/
structure SnapshotProjection (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (output : Format.HeapSnapshot) : Prop where
  roots : CellsProjection heap state.copying heap.initial.roots.toList state.roots output.roots.toList
  objects : ∃ originals copies,
    output.objects.toList = originals ++ copies ∧
    ListProjection (OriginalProjection heap state units collection) heap.initial.objects.toList originals ∧
    ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx copies
  externalWeak : UnitsProjection heap state.copying units heap.initial.externalWeak output.externalWeak
  remembered : output.remembered = heap.initial.remembered

private theorem originalReference_correct (heap : PreparedHeap n k) (initial : Format.Allocation)
    (object : Fin n) (accepted : originalReference heap.objectOfAllocation initial.allocationId = .ok object) :
    OriginalObject heap initial object := by
  unfold originalReference at accepted
  cases checked : checkedFin initial.allocationId.value n with
  | error message => simp [checked, bind, Except.bind] at accepted
  | ok index =>
      simp [checked, bind, Except.bind, pure, Except.pure] at accepted
      exact ⟨index, (checkedFin_correct _ _ _).mp checked, accepted⟩

private theorem originalState_projects (initial output : Format.ObjectState) (header : Format.Header)
    (accepted : originalState initial header = .ok output) :
    OriginalStateProjection initial header output := by
  cases initial <;> cases header <;>
    simp [originalState, pure, Except.pure] at accepted <;> cases accepted <;> constructor

private theorem captureOriginal_projects (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (initial output : Format.Allocation)
    (accepted : captureOriginal heap state units collection initial = .ok output) :
    OriginalProjection heap state units collection initial output := by
  unfold captureOriginal at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    refine ⟨⟨_, originalReference_correct _ _ _ (by assumption), ?_,
      (mergeReferences_correct _ _ _ _ _).mp (by assumption), ?_⟩, rfl, rfl⟩
    · have projected := originalState_projects _ _ _ (by assumption)
      try
        have converted := (allocationOfReference_correct heap state.copying _ _).mp (by assumption)
        obtain ⟨_, rfl⟩ := converted
      simp only [encodeHeader, *]
    · split <;> first | rfl | contradiction | exact (captureUnits_correct _ _ _ _ _).mp (by assumption)

private theorem captureCopy_projects (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (pair : Copying.CopyRecord n × Nat) (output : Format.Allocation)
    (accepted : captureCopy heap state units collection pair.1 pair.2 = .ok output) :
    CopyProjection heap state units pair output := by
  refine ⟨captureCopy_identity _ _ _ _ _ _ _ accepted, captureCopy_strong _ _ _ _ _ _ _ accepted, ?_⟩
  unfold captureCopy at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    exact (captureUnits_correct _ _ _ _ _).mp (by assumption)

/-- Every successful full or young evacuation snapshot has the complete projection. -/
theorem captureStrong_projects (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (output : Format.HeapSnapshot) (accepted : captureStrong heap state units collection = .ok output) :
    SnapshotProjection heap state units collection output := by
  have roots := captureStrong_roots heap state units collection output accepted
  unfold captureStrong at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    refine ⟨roots, ⟨_, _, rfl, ?_, ?_⟩, (captureUnits_correct _ _ _ _ _).mp (by assumption), rfl⟩
    · exact mapM_projects _ _ (captureOriginal_projects heap state units collection) _ _ (by assumption)
    · exact mapM_projects _ _ (captureCopy_projects heap state units collection) _ _ (by assumption)

/-- Full weak evacuation uses the same complete physical projection. -/
theorem captureEvacuated_projects (heap : PreparedHeap n k) (state : Copying.Weak.State n k)
    (output : Format.HeapSnapshot) (accepted : captureEvacuated heap state = .ok output) :
    SnapshotProjection heap state.strong state.units .full output := by
  unfold captureEvacuated at accepted
  simp only [bind, Except.bind] at accepted
  split at accepted
  · exact captureStrong_projects heap state.strong state.units .full output accepted
  · contradiction

end CloverGC.Differential
