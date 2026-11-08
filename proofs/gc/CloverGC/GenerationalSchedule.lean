import CloverGC.GenerationalFields

namespace CloverGC.Generational

open Copying

structure Ready (input : CollectionInput n) (done scanned : List (Fin n))
    (logical : MarkState n) (state : EvacuationState n) : Prop where
  tracking : Tracking input state.copying logical
  fields : FieldsInvariant input done scanned state
  closed : Closed input logical (done ++ scanned)
  done_outside : ∀ o ∈ done, input.selected o = false
  scanned_marked : ∀ o ∈ scanned, o ∈ logical.marked
  roots_known : ∀ o ∈ input.roots, input.selected o = true → o ∈ logical.marked
  roots_repaired : state.roots = input.roots.map (canonical state.copying)

theorem discoverMany_append (left right : List (Fin n)) (logical : MarkState n) :
    discoverMany (left ++ right) logical = discoverMany right (discoverMany left logical) := by
  induction left generalizing logical with
  | nil => rfl
  | cons head rest ih => simpa [discoverMany] using ih (CloverGC.discover head logical)

theorem ready_seed (input : CollectionInput n) :
    Ready input [] [] (seed (input.roots.filter input.selected)) (seedRoots input) := by
  let initial : EvacuationState n := ⟨.empty, rawFields input.copyInput, []⟩
  have ht := repair_tracking input .empty .empty input.roots (tracking_empty input)
  have hf : FieldsInvariant input [] [] initial := ⟨by intro o; simp [initial, originalPlan], by simp [initial, CopyState.empty]⟩
  have repaired := fields_repair input initial .empty [] [] input.roots (tracking_empty input) hf
    (by simp [Closed]) (by simp)
  refine ⟨ht, ⟨repaired.originals, repaired.copies⟩, ?_, by simp, by simp, ?_, ?_⟩
  · simp [Closed]
  · intro o member selected
    exact (discoverMany_marked _ .empty o).mpr (Or.inr (List.mem_filter.mpr ⟨member, selected⟩))
  · exact repair_outputs input .empty .empty input.roots (tracking_empty input)

theorem ready_remember_step (input : CollectionInput n) (done : List (Fin n))
    (logical : MarkState n) (state : EvacuationState n) (owner : Fin n)
    (ready : Ready input done [] logical state) (outside : input.selected owner = false)
    (fresh : owner ∉ done) :
    let children := input.heap.data.graph.strongChildren owner
    let result := repair input state.copying (state.originalFields owner)
    Ready input (done ++ [owner]) [] (discoverMany (children.filter input.selected) logical)
      (writeFields { state with copying := result.1 } (.source owner) result.2) := by
  have raw : state.originalFields owner = (input.heap.data.graph.strongChildren owner).map Reference.source := by
    simpa [originalPlan, fresh, rawFields, CollectionInput.copyInput] using ready.fields.originals owner
  rw [raw]
  let children := input.heap.data.graph.strongChildren owner
  let result := repair input state.copying (children.map Reference.source)
  let next : EvacuationState n := { state with copying := result.1 }
  let logicalNext := discoverMany (children.filter input.selected) logical
  have ht : Tracking input next.copying logicalNext := repair_tracking input state.copying logical children ready.tracking
  have hf : FieldsInvariant input done [] next := fields_repair input state logical done [] children ready.tracking
    ready.fields ready.closed ready.scanned_marked
  have values : result.2 = children.map (canonical next.copying) := repair_outputs input state.copying logical children ready.tracking
  change Ready input (done ++ [owner]) [] logicalNext (writeFields next (.source owner) result.2)
  rw [values]
  refine ⟨tracking_write input next logicalNext (.source owner) _ ht, fields_remember input next done owner hf,
    ?_, ?_, by simp, ?_, ?_⟩
  · intro parent member child edge selected
    simp only [List.append_nil, List.mem_append, List.mem_singleton] at member
    rcases member with old | rfl
    · exact (discoverMany_marked _ _ child).mpr (Or.inl (ready.closed parent (by simpa using old) child edge selected))
    · exact (discoverMany_marked _ _ child).mpr (Or.inr (List.mem_filter.mpr ⟨edge, selected⟩))
  · intro o member
    rcases List.mem_append.mp member with old | new
    · exact ready.done_outside o old
    · exact List.mem_singleton.mp new ▸ outside
  · intro o member selected
    exact (discoverMany_marked _ _ o).mpr (Or.inl (ready.roots_known o member selected))
  · rw [write_roots, write_canonical]
    exact ready.roots_repaired.trans (List.map_congr_left fun o member =>
      (repair_stable input state.copying logical children ready.tracking o (ready.roots_known o member)).symm)

theorem ready_remember (input : CollectionInput n) (done owners : List (Fin n))
    (logical : MarkState n) (state : EvacuationState n) (ready : Ready input done [] logical state)
    (unique : (done ++ owners).Nodup) (outside : ∀ o ∈ owners, input.selected o = false) :
    Ready input (done ++ owners) []
      (discoverMany ((owners.flatMap input.heap.data.graph.strongChildren).filter input.selected) logical)
      (remember input state owners) := by
  induction owners generalizing state done logical with
  | nil => simpa [remember, discoverMany] using ready
  | cons owner rest ih =>
      have split := List.nodup_append.mp unique
      have notDone : owner ∉ done := by
        intro mem
        exact split.2.2 owner mem owner (by simp) rfl
      have step := ready_remember_step input done logical state owner ready (outside owner (by simp)) notDone
      have uniqueNext : ((done ++ [owner]) ++ rest).Nodup := by simpa [List.append_assoc] using unique
      have result := ih (done ++ [owner]) _ _ step uniqueNext (fun o ho => outside o (by simp [ho]))
      simpa [remember, List.flatMap_cons, List.filter_append, discoverMany_append, List.append_assoc] using result

theorem ready_start (input : CollectionInput n) :
    Ready input input.remembered [] (seed input.logicalRoots) (start input) := by
  have result := ready_remember input [] input.remembered _ _ (ready_seed input)
    (by simpa using remembered_nodup input) (remembered_unselected input)
  simpa [start, CollectionInput.logicalRoots, List.filter_append, seed, discoverMany_append] using result

theorem ready_scan (input : CollectionInput n) (done scanned : List (Fin n))
    (logical : MarkState n) (state : EvacuationState n) (object : Fin n) (rest : List (Fin n))
    (ready : Ready input done scanned logical state)
    (marking : Invariant input.selectedGraph input.logicalRoots scanned logical)
    (queue : logical.pending = object :: rest) :
    ∃ next, scan input state = some (canonical state.copying object, next) ∧
      Ready input done (object :: scanned)
        (discoverMany (input.selectedGraph.strongChildren object) ⟨logical.marked, rest⟩) next ∧
      ∀ o, (input.selected o = true → o ∈ logical.marked) →
        canonical next.copying o = canonical state.copying o := by
  have member : object ∈ logical.marked := ready.tracking.included object (by simp [queue])
  have inside := ready.tracking.marked_selected object member
  have notDone : object ∉ done := by
    intro mem
    have outside := ready.done_outside object mem
    simp [inside] at outside
  have notScanned := marking.disjoint object (by simp [queue])
  let ref := canonical state.copying object
  have represented : Represents input.copyInput state.copying object ref := ready.tracking.schedule.known object member
  have physical : state.copying.pending = ref :: rest.map (canonical state.copying) := by
    simp [ready.tracking.schedule.pending, queue, ref]
  have raw : liveFields state ref = rawFields input.copyInput object := by
    cases eq : ref with
    | source other =>
        have rep := represented
        rw [eq] at rep
        obtain ⟨same, stationary⟩ := rep
        subst other
        simpa [liveFields, eq, originalPlan, notDone, notScanned] using ready.fields.originals object
    | copied index =>
        have rep := represented
        rw [eq] at rep
        obtain ⟨record, get, origin⟩ := ready.tracking.copying.ledger.forwarding_valid object index rep
        simpa [liveFields, eq, get, origin, copyPlan, notScanned] using ready.fields.copies index record get
  let popped : CopyState n := { state.copying with pending := rest.map (canonical state.copying) }
  let logicalPopped : MarkState n := ⟨logical.marked, rest⟩
  have ht : Tracking input popped logicalPopped := {
    copying := pop_preserves input.copyInput state.copying ref _ ready.tracking.copying physical
    selected := ready.tracking.selected
    schedule := ⟨ready.tracking.schedule.marked, rfl, ready.tracking.schedule.known⟩
    included := fun o ho => ready.tracking.included o (queue.symm ▸ List.mem_cons_of_mem object ho)
    marked_selected := ready.tracking.marked_selected }
  let children := input.heap.data.graph.strongChildren object
  let result := repair input popped (children.map Reference.source)
  let prepared : EvacuationState n := { state with copying := result.1 }
  let logicalNext := discoverMany (children.filter input.selected) logicalPopped
  let next := writeFields prepared ref result.2
  have repairedTrack : Tracking input prepared.copying logicalNext := repair_tracking input popped logicalPopped children ht
  have poppedFields : FieldsInvariant input done scanned { state with copying := popped } :=
    ⟨ready.fields.originals, ready.fields.copies⟩
  have repairedFields := fields_repair input { state with copying := popped } logicalPopped done scanned children ht
    poppedFields ready.closed ready.scanned_marked
  have values : result.2 = children.map (canonical prepared.copying) := repair_outputs input popped logicalPopped children ht
  have stable (o : Fin n) (known : input.selected o = true → o ∈ logical.marked) :
      canonical prepared.copying o = canonical state.copying o :=
    repair_stable input popped logicalPopped children ht o known
  have parentKnown : object ∈ logicalNext.marked := (discoverMany_marked _ _ object).mpr (Or.inl member)
  have repNext : Represents input.copyInput prepared.copying object ref := by
    have known := repairedTrack.schedule.known object parentKnown
    rw [stable object (fun _ => member)] at known
    exact known
  refine ⟨next, ?_, ?_, ?_⟩
  · simp [scan, physical, raw, rawFields, CollectionInput.copyInput, next, prepared, result, popped, children, ref]
  · change Ready input done (object :: scanned) logicalNext next
    refine ⟨tracking_write input prepared logicalNext ref result.2 repairedTrack, ?_, ?_, ready.done_outside,
      ?_, ?_, ?_⟩
    · change FieldsInvariant input done (object :: scanned) (writeFields prepared ref result.2)
      rw [values]
      exact fields_write input prepared done scanned object ref repairedTrack.copying repairedFields repNext
    · intro owner mem child edge selected
      have choices : owner ∈ done ∨ owner = object ∨ owner ∈ scanned := by simpa using mem
      rcases choices with old | rfl | old
      · exact (discoverMany_marked _ _ child).mpr (Or.inl (ready.closed owner (by simp [old]) child edge selected))
      · exact (discoverMany_marked _ _ child).mpr (Or.inr (List.mem_filter.mpr ⟨edge, selected⟩))
      · exact (discoverMany_marked _ _ child).mpr (Or.inl (ready.closed owner (by simp [old]) child edge selected))
    · intro o mem
      rcases List.mem_cons.mp mem with rfl | old
      · exact parentKnown
      · exact (discoverMany_marked _ _ o).mpr (Or.inl (ready.scanned_marked o old))
    · intro o mem selected
      exact (discoverMany_marked _ _ o).mpr (Or.inl (ready.roots_known o mem selected))
    · rw [write_roots, write_canonical]
      exact ready.roots_repaired.trans (List.map_congr_left fun o mem => (stable o (ready.roots_known o mem)).symm)
  · intro o known
    exact (congrFun (write_canonical prepared ref result.2) o).trans (stable o known)

theorem drain_simulation (input : CollectionInput n) (done scanned : List (Fin n)) (fuel : Nat)
    (logical : MarkState n) (state : EvacuationState n)
    (ready : Ready input done scanned logical state)
    (marking : Invariant input.selectedGraph input.logicalRoots scanned logical) :
    let logicalResult := CloverGC.drain input.selectedGraph fuel logical
    let result := drain input fuel state
    Ready input done (logicalResult.2.reverse ++ scanned) logicalResult.1 result.1 ∧
    result.2 = logicalResult.2.map (canonical result.1.copying) ∧
    (∀ o, (input.selected o = true → o ∈ logical.marked) → canonical result.1.copying o = canonical state.copying o) := by
  induction fuel generalizing state logical scanned with
  | zero =>
      simp only [drain, CloverGC.drain, List.reverse_nil, List.nil_append, List.map_nil]
      exact ⟨ready, trivial, by intro o _; trivial⟩
  | succ fuel ih =>
      cases queue : logical.pending with
      | nil =>
          have physical : state.copying.pending = [] := by simp [ready.tracking.schedule.pending, queue]
          simpa [drain, scan, physical, CloverGC.drain, scanOne, queue] using
            (show Ready input done scanned logical state ∧ ([] : List (Reference n)) = [] ∧
              (∀ o, (input.selected o = true → o ∈ logical.marked) → canonical state.copying o = canonical state.copying o) from
              ⟨ready, rfl, by intro o _; rfl⟩)
      | cons object rest =>
          obtain ⟨next, step, readyNext, stable⟩ := ready_scan input done scanned logical state object rest ready marking queue
          have markingNext := scan_preserves input.selectedGraph input.logicalRoots scanned logical object rest marking queue
          obtain ⟨final, trace, stableTail⟩ := ih (object :: scanned) _ next readyNext markingNext
          simp only [CloverGC.drain, scanOne, queue, drain, step]
          refine ⟨?_, ?_, ?_⟩
          · simpa [List.reverse_cons, List.append_assoc] using final
          · have oldMarked : object ∈ logical.marked := (marking.accounted object).mpr (Or.inl (by simp [queue]))
            have newMarked : object ∈ (discoverMany (input.selectedGraph.strongChildren object)
                (⟨logical.marked, rest⟩ : MarkState n)).marked :=
              (discoverMany_marked _ _ object).mpr (Or.inl oldMarked)
            exact congr (congrArg List.cons ((stableTail object (fun _ => newMarked)).trans
              (stable object (fun _ => oldMarked))).symm) trace
          · intro o known
            exact (stableTail o (fun selected => (discoverMany_marked _ _ o).mpr (Or.inl (known selected)))).trans (stable o known)

theorem evacuate_invariant (input : CollectionInput n) :
    Ready input input.remembered (collect input.selectedGraph input.logicalRoots).2.reverse
      (collect input.selectedGraph input.logicalRoots).1 (evacuate input).1 := by
  simpa [collect, evacuate] using (drain_simulation input input.remembered [] n _ _ (ready_start input)
    (seed_invariant input.selectedGraph input.logicalRoots)).1

theorem evacuate_trace (input : CollectionInput n) :
    (evacuate input).2 = (collect input.selectedGraph input.logicalRoots).2.map (canonical (evacuate input).1.copying) := by
  exact (drain_simulation input input.remembered [] n _ _ (ready_start input)
    (seed_invariant input.selectedGraph input.logicalRoots)).2.1

theorem evacuate_complete (input : CollectionInput n) : (evacuate input).1.copying.pending = [] := by
  have inv := evacuate_invariant input
  simp [inv.tracking.schedule.pending, collect_complete]

end CloverGC.Generational
