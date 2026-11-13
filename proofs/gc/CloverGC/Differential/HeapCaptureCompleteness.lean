import CloverGC.Differential.HeapWireCaptureValidity
import CloverGC.Differential.HeapStrongCaptureValidity
import CloverGC.Differential.HeapWeakCaptureValidity

namespace CloverGC.Differential

private theorem CellsProjection.of_model (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (model : ModelStrongReferences heap.objectOfAllocation layout sourceRefs)
    (refs : List (Copying.Reference n)) (cardinality : refs.length = sourceRefs.length)
    (safe : ∀ reference ∈ refs, Copying.ValidReference copying reference) :
    ∃ output, CellsProjection heap copying layout refs output := by
  induction model generalizing refs with
  | nil =>
      have empty : refs = [] := List.eq_nil_of_length_eq_zero cardinality
      subst refs
      exact ⟨[], .nil⟩
  | immediate tail ih =>
      obtain ⟨output, projected⟩ := ih refs cardinality safe
      exact ⟨_, .immediate _ projected⟩
  | null tail ih =>
      obtain ⟨output, projected⟩ := ih refs cardinality safe
      exact ⟨_, .null projected⟩
  | reference _ tail ih =>
      cases refs with
      | nil => simp at cardinality
      | cons reference rest =>
          obtain ⟨output, projected⟩ := ih rest (by simpa using cardinality)
            (fun ref member => safe ref (by simp [member]))
          exact ⟨_, .reference _ reference (safe reference (by simp)) projected⟩

/-- Slot cardinality and allocated references suffice for actual strong capture;
    immediates and nulls are retained at their original positions. -/
theorem mergeReferences_succeeds_of_model (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (model : ModelStrongReferences heap.objectOfAllocation layout sourceRefs)
    (refs : List (Copying.Reference n)) (cardinality : refs.length = sourceRefs.length)
    (safe : ∀ reference ∈ refs, Copying.ValidReference copying reference) :
    ∃ output, mergeReferences (allocationOfReference heap copying) layout refs = .ok output ∧
      CellsProjection heap copying layout refs output := by
  obtain ⟨output, projected⟩ := CellsProjection.of_model heap copying model refs cardinality safe
  exact ⟨output, (mergeReferences_correct _ _ _ _ _).mpr projected, projected⟩

/-- A bounded, kind-matching unit with allocated references captures successfully. -/
private theorem captureUnit_succeeds (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout : Format.WeakUnit) (id : Fin k)
    (identity : id.val = layout.unitId.value) (kind : UnitKindMatches layout.contents (units id))
    (safe : UnitReferencesValid copying (units id)) :
    ∃ output, captureUnit heap copying units layout = .ok output ∧
      UnitProjection heap copying units layout output := by
  have projected : UnitProjection heap copying units layout ⟨layout.unitId, encodeUnit heap (units id)⟩ :=
    ⟨id, identity, kind, safe, rfl⟩
  exact ⟨_, (captureUnit_correct _ _ _ _ _).mpr projected, projected⟩

/-- The whole weak layout captures from semantic unit bounds, kinds and allocation validity. -/
theorem captureUnits_succeeds (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) (layout : Array Format.WeakUnit)
    (each : ∀ unit ∈ layout.toList, ∃ id : Fin k, id.val = unit.unitId.value ∧
      UnitKindMatches unit.contents (units id) ∧ UnitReferencesValid copying (units id)) :
    ∃ output, captureUnits heap copying units layout = .ok output ∧
      UnitsProjection heap copying units layout output := by
  obtain ⟨output, _, projected⟩ := mapM_success layout.toList (captureUnit heap copying units)
    (UnitProjection heap copying units) (by
      intro unit member
      obtain ⟨id, identity, kind, safe⟩ := each unit member
      exact captureUnit_succeeds heap copying units unit id identity kind safe)
  have vectorProjection : UnitsProjection heap copying units layout output.toArray := by
    simpa only [UnitsProjection, List.toList_toArray] using projected
  exact ⟨_, (captureUnits_correct _ _ _ _ _).mpr vectorProjection, vectorProjection⟩

/-- Movement-kind correspondence permits every normal header, and forwarding
    only for movable source storage. -/
private theorem originalState_succeeds_of_kind (before : Format.ObjectState) (kind : Generational.OriginalKind)
    (model : ModelOriginalKind before kind) (header : Format.Header)
    (forwarded : ∀ target, header = .forwarded target → kind.movable = true) :
    ∃ after, originalState before header = .ok after := by
  cases before with
  | youngOriginal policy old => exact ⟨.youngOriginal policy header, rfl⟩
  | oldOriginal policy old => exact ⟨.oldOriginal policy header, rfl⟩
  | nonmovingOriginal marked =>
      have immovable : kind.movable = false := by
        cases kind <;> simp_all [ModelOriginalKind, Generational.OriginalKind.movable]
      cases header with
      | normal marked => exact ⟨.nonmovingOriginal marked, rfl⟩
      | forwarded target => simp [immovable] at forwarded
  | ordinaryDestination | nonmovingDestination =>
      cases kind <;> simp [ModelOriginalKind] at model

/-- Every selected allocated destination captures from the copy ledger and
    reference-valid fields and units, without assuming executable acceptance. -/
private theorem captureCopy_succeeds (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (record : Copying.CopyRecord n) (index : Nat)
    (invariant : Copying.CopyInvariant heap.copying state.copying)
    (allocated : state.copying.copies[index]? = some record)
    (selected : (heap.collectionInput collection).selected record.origin = true)
    (model : ModelStrongReferences heap.objectOfAllocation (heap.originals.get record.origin).strong.toList sourceRefs)
    (cardinality : record.strongChildren.length = sourceRefs.length)
    (safe : ∀ reference ∈ record.strongChildren, Copying.ValidReference state.copying reference)
    (weakReady : ∀ unit ∈ (heap.originals.get record.origin).weak.toList, ∃ id : Fin k,
      id.val = unit.unitId.value ∧ UnitKindMatches unit.contents (units id) ∧ UnitReferencesValid state.copying (units id)) :
    ∃ output, captureCopy heap state units collection record index = .ok output := by
  obtain ⟨strong, strongChecked, _⟩ := mergeReferences_succeeds_of_model heap state.copying model record.strongChildren cardinality safe
  obtain ⟨weak, weakChecked, _⟩ := captureUnits_succeeds heap state.copying units (heap.originals.get record.origin).weak weakReady
  have movable := (invariant.ledger.copies_metadata index record allocated).1
  have marked := invariant.copies_marked index record allocated
  let after := match record.destination with
    | .ordinary => Format.ObjectState.ordinaryDestination
    | .nonMoving => Format.ObjectState.nonmovingDestination
  let output : Format.Allocation := ⟨(heap.originals.get record.origin).objectId, ⟨n + index⟩, after, strong.toArray, weak⟩
  refine ⟨output, ?_⟩
  simp [captureCopy, selected, movable, marked, strongChecked, weakChecked, output, after,
    bind, Except.bind, pure, Except.pure]
  cases record.destination <;> rfl

/-- Source capture follows the copy ledger, preserves unselected old storage,
    and retains the physical slot layout. -/
private theorem captureOriginal_succeeds (heap : PreparedHeap n k) (state : Copying.EvacuationState n)
    (units : Fin k → Copying.Weak.Unit n) (collection : Generational.CollectionKind)
    (initial : Format.Allocation) (object : Fin n)
    (mapped : OriginalObject heap initial object)
    (kind : ModelOriginalKind initial.state (heap.generational.data.objectKind object))
    (invariant : Copying.CopyInvariant heap.copying state.copying)
    (outside : (heap.collectionInput collection).selected object = false →
      state.copying.forwarding object = none ∧ .source object ∉ state.copying.marked)
    (model : ModelStrongReferences heap.objectOfAllocation initial.strong.toList sourceRefs)
    (cardinality : (state.originalFields object).length = sourceRefs.length)
    (safe : ∀ reference ∈ state.originalFields object, Copying.ValidReference state.copying reference)
    (weakReady : ∀ unit ∈ initial.weak.toList, ∃ id : Fin k,
      id.val = unit.unitId.value ∧ UnitKindMatches unit.contents (units id) ∧ UnitReferencesValid state.copying (units id)) :
    ∃ output, captureOriginal heap state units collection initial = .ok output := by
  have reference : originalReference heap.objectOfAllocation initial.allocationId = .ok object :=
    (originalReference_projects _ _ _).mpr mapped
  obtain ⟨strong, strongChecked, _⟩ := mergeReferences_succeeds_of_model heap state.copying model (state.originalFields object) cardinality safe
  obtain ⟨weak, weakChecked, _⟩ := captureUnits_succeeds heap state.copying units initial.weak weakReady
  have unmarked (moving : heap.copying.movable object = true) : .source object ∉ state.copying.marked := by
    intro marked
    have canonical := invariant.marked_canonical (.source object) marked
    simp [Copying.Canonical, moving] at canonical
  cases forward : state.copying.forwarding object with
  | none =>
      obtain ⟨after, updated⟩ := originalState_succeeds_of_kind initial.state _ kind
        (.normal (decide (.source object ∈ state.copying.marked))) (by intros; contradiction)
      cases selected : (heap.collectionInput collection).selected object with
      | false =>
          have clear := (outside selected).2
          simp only [clear, decide_false] at updated
          simp [captureOriginal, reference, selected, forward, clear, updated,
            strongChecked, weakChecked, bind, Except.bind, pure, Except.pure]
      | true =>
          cases moving : heap.copying.movable object with
          | false =>
              simp [captureOriginal, reference, selected, moving, forward, updated,
                strongChecked, weakChecked, bind, Except.bind, pure, Except.pure]
          | true =>
              have clear := unmarked moving
              simp only [clear, decide_false] at updated
              simp [captureOriginal, reference, selected, moving, forward, clear, updated,
                strongChecked, bind, Except.bind, pure, Except.pure]
  | some index =>
      obtain ⟨record, allocated, origin⟩ := invariant.ledger.forwarding_valid object index forward
      have moving : heap.copying.movable object = true := by
        rw [← origin]
        exact (invariant.ledger.copies_metadata index record allocated).1
      have selected : (heap.collectionInput collection).selected object = true := by
        cases select : (heap.collectionInput collection).selected object with
        | false => have unchanged := (outside select).1; simp [forward] at unchanged
        | true => rfl
      have converted : allocationOfReference heap state.copying (.copied index) =
          .ok (encodeReference heap (.copied index)) :=
        (allocationOfReference_correct _ _ _ _).mpr ⟨⟨record, allocated⟩, rfl⟩
      obtain ⟨after, updated⟩ := originalState_succeeds_of_kind initial.state _ kind
        (.forwarded (encodeReference heap (.copied index))) (by intros; exact moving)
      have clear := unmarked moving
      simp [captureOriginal, reference, selected, moving, forward, clear, converted, updated,
        strongChecked, bind, Except.bind, pure, Except.pure]

/-- Every retained wire unit has a bounded ID, matching kind and allocated references. -/
def WeakCaptureReady (heap : PreparedHeap n k) (copying : Copying.CopyState n)
    (units : Fin k → Copying.Weak.Unit n) : Prop :=
  ∀ unit ∈ heap.initial.objects.toList.flatMap (fun a => a.weak.toList) ++ heap.initial.externalWeak.toList,
    ∃ id : Fin k, id.val = unit.unitId.value ∧ UnitKindMatches unit.contents (units id) ∧
      UnitReferencesValid copying (units id)

/-- A proof-carrying weak input for the already prepared generational heap. -/
def preparedWeakInput (heap : PreparedHeap n k)
    (valid : Generational.Weak.WeakWellFormed ⟨heap.generational, heap.weak⟩)
    (collection : Generational.CollectionKind) : Generational.Weak.Input n k :=
  ⟨⟨⟨heap.generational, heap.weak⟩, valid⟩, collection, heap.roots⟩

theorem preparedFullCapture_ready {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    (projected : PreparedHeapProjection initial heap) :
    let state := Copying.Weak.collectWeak heap.copying heap.weak heap.roots
    StrongCaptureReady heap state.strong .full ∧ state.pending = [] ∧ WeakCaptureReady heap state.strong.copying state.units := by
  refine ⟨?_, Copying.Weak.collection_weak_queue_empty _ _ _, ?_⟩
  · rw [Copying.Weak.collectWeak_strong]
    exact strongCaptureReady_full heap
  · intro unit member
    rw [projected.snapshot] at member
    obtain ⟨id, identity, model⟩ := projected.templates unit member
    obtain ⟨kind, safe⟩ := weakCaptureReady_full heap.copying heap.weak heap.roots projected.weakValid.storage id model.initial_kind
    exact ⟨id, identity, kind, safe⟩

theorem preparedGenerationalCapture_ready {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    (projected : PreparedHeapProjection initial heap)
    (collection : Generational.CollectionKind) :
    let state := (Generational.Weak.collectWeak (preparedWeakInput heap projected.weakValid collection)).1
    StrongCaptureReady heap state.strong collection ∧ state.pending = [] ∧ WeakCaptureReady heap state.strong.copying state.units := by
  let input := preparedWeakInput heap projected.weakValid collection
  refine ⟨?_, Generational.Weak.collection_weak_queue_empty input, ?_⟩
  · have same := congrArg Prod.fst (Generational.Weak.collectWeak_strong input)
    change StrongCaptureReady heap (Generational.Weak.collectWeak input).1.strong collection
    dsimp only at same
    rw [same]
    exact strongCaptureReady_generational heap collection
  · intro unit member
    rw [projected.snapshot] at member
    obtain ⟨id, identity, model⟩ := projected.templates unit member
    obtain ⟨kind, safe⟩ := weakCaptureReady_generational input id model.initial_kind
    exact ⟨id, identity, kind, safe⟩

/-- Every physical original is captured, in input order, from the independent prepared model. -/
theorem captureOriginals_succeeds {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    {state : Copying.EvacuationState initial.objects.size} {units : Fin k → Copying.Weak.Unit initial.objects.size}
    {collection : Generational.CollectionKind}
    (projected : PreparedHeapProjection initial heap)
    (valid : Format.FreshWellFormed initial) (ready : StrongCaptureReady heap state collection)
    (weak : WeakCaptureReady heap state.copying units) :
    ∃ output, heap.initial.objects.toList.mapM (captureOriginal heap state units collection) = .ok output ∧
      ListProjection (OriginalProjection heap state units collection) heap.initial.objects.toList output := by
  obtain ⟨output, checked, projectedOutput⟩ := mapM_success heap.initial.objects.toList (captureOriginal heap state units collection)
    (OriginalProjection heap state units collection) (by
      intro a member
      have initialMember : a ∈ initial.objects.toList := by simpa only [projected.snapshot] using member
      obtain ⟨object, mapped, stored⟩ := projected.original_object valid a initialMember
      have model := projected.children object
      have kind := projected.kinds object
      rw [stored] at model kind
      have weakReady : ∀ unit ∈ a.weak.toList, ∃ id : Fin _, id.val = unit.unitId.value ∧
          UnitKindMatches unit.contents (units id) ∧ UnitReferencesValid state.copying (units id) := by
        intro unit owned
        exact weak unit (List.mem_append_left _ (List.mem_flatMap.mpr ⟨a, member, owned⟩))
      obtain ⟨output, captured⟩ := captureOriginal_succeeds heap state units collection a object mapped kind
        ready.copying (ready.unselected object) model (ready.originals object).1 (ready.originals object).2 weakReady
      exact ⟨output, captured, captureOriginal_projects heap state units collection a output captured⟩)
  exact ⟨output, checked, projectedOutput⟩

/-- Every allocated destination is captured once, in allocation order. -/
theorem captureCopies_succeeds {initial : Format.HeapSnapshot} {heap : PreparedHeap initial.objects.size k}
    {state : Copying.EvacuationState initial.objects.size} {units : Fin k → Copying.Weak.Unit initial.objects.size}
    {collection : Generational.CollectionKind}
    (projected : PreparedHeapProjection initial heap)
    (valid : Format.FreshWellFormed initial) (ready : StrongCaptureReady heap state collection)
    (weak : WeakCaptureReady heap state.copying units) :
    ∃ output, state.copying.copies.zipIdx.mapM (fun (record, index) => captureCopy heap state units collection record index) = .ok output ∧
      ListProjection (CopyProjection heap state units) state.copying.copies.zipIdx output := by
  obtain ⟨output, checked, projectedOutput⟩ := mapM_success state.copying.copies.zipIdx
    (fun (record, index) => captureCopy heap state units collection record index) (CopyProjection heap state units) (by
      intro pair member
      have allocated : state.copying.copies[pair.2]? = some pair.1 := List.mem_zipIdx_iff_getElem?.mp member
      have sourceMember : heap.originals.get pair.1.origin ∈ heap.initial.objects.toList := by
        rw [projected.snapshot]
        exact projected.originals.member projected.objectIds valid.identities pair.1.origin
      have weakReady : ∀ unit ∈ (heap.originals.get pair.1.origin).weak.toList, ∃ id : Fin _, id.val = unit.unitId.value ∧
          UnitKindMatches unit.contents (units id) ∧ UnitReferencesValid state.copying (units id) := by
        intro unit owned
        exact weak unit (List.mem_append_left _ (List.mem_flatMap.mpr ⟨_, sourceMember, owned⟩))
      obtain ⟨output, captured⟩ := captureCopy_succeeds heap state units collection pair.1 pair.2 ready.copying allocated
        (ready.selected _ _ allocated) (projected.children pair.1.origin) (ready.copies _ _ allocated).1
        (ready.copies _ _ allocated).2 weakReady
      exact ⟨output, captured, captureCopy_projects heap state units collection pair output captured⟩)
  exact ⟨output, checked, projectedOutput⟩

end CloverGC.Differential
