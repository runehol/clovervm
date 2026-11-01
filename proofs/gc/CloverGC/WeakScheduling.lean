import CloverGC.WeakResolutionProofs

namespace CloverGC.Copying.Weak

@[simp] theorem earlyVisit_strong (input : CopyInput n) (state : State n k) (id : Fin k) :
    (earlyVisit input state id).strong = state.strong := rfl

@[simp] theorem earlyVisits_strong (input : CopyInput n) (state : State n k) (ids : List (Fin k)) :
    (earlyVisits input state ids).strong = state.strong := by
  induction ids generalizing state with
  | nil => rfl
  | cons id rest ih => exact ih (earlyVisit input state id)

@[simp] theorem lateVisit_strong (input : CopyInput n) (state : State n k) (id : Fin k) :
    (lateVisit input state id).strong = state.strong := rfl

@[simp] theorem lateVisits_strong (input : CopyInput n) (state : State n k) (ids : List (Fin k)) :
    (lateVisits input state ids).strong = state.strong := by
  induction ids generalizing state with
  | nil => rfl
  | cons id rest ih => exact ih (lateVisit input state id)

theorem drainWeak_strong (input : CopyInput n) (weak : Input n k) (fuel : Nat) (state : State n k) :
    (drainWeak input weak fuel state).strong = (drainCopies input fuel state.strong).1 := by
  induction fuel generalizing state with
  | zero => rfl
  | succ fuel ih =>
      cases step : scanCopy input state.strong with
      | none => simp [drainWeak, drainCopies, step]
      | some pair =>
          simpa [drainWeak, drainCopies, step] using
            ih (earlyVisits input { state with strong := pair.2 } (ownerUnitsFor weak pair.2 pair.1))

/-- Non-retention is a structural erasure theorem: weak work changes no strong
    state at all in this representative payload model. -/
theorem collectWeak_strong (input : CopyInput n) (weak : Input n k) (roots : List (Fin n)) :
    (collectWeak input weak roots).strong = (evacuate input roots).1 := by
  simp [collectWeak, finishDeferred, drainWeak_strong, start, evacuate]

@[simp] theorem earlyVisits_registered (input : CopyInput n) (state : State n k) (ids : List (Fin k)) :
    (earlyVisits input state ids).registered = state.registered ++ ids := by
  induction ids generalizing state with
  | nil => simp [earlyVisits]
  | cons id rest ih => simpa [earlyVisits, earlyVisit, List.append_assoc] using ih (earlyVisit input state id)

@[simp] theorem lateVisits_registered (input : CopyInput n) (state : State n k) (ids : List (Fin k)) :
    (lateVisits input state ids).registered = state.registered := by
  induction ids generalizing state with
  | nil => rfl
  | cons id rest ih => exact ih (lateVisit input state id)

@[simp] theorem earlyVisits_lateVisits (input : CopyInput n) (state : State n k) (ids : List (Fin k)) :
    (earlyVisits input state ids).lateVisits = state.lateVisits := by
  induction ids generalizing state with
  | nil => rfl
  | cons id rest ih => exact ih (earlyVisit input state id)

@[simp] theorem lateVisits_trace (input : CopyInput n) (state : State n k) (ids : List (Fin k)) :
    (lateVisits input state ids).lateVisits = state.lateVisits ++ ids := by
  induction ids generalizing state with
  | nil => simp [lateVisits]
  | cons id rest ih => simpa [lateVisits, lateVisit, List.append_assoc] using ih (lateVisit input state id)

theorem ownerUnitsFor_represents (input : CopyInput n) (weak : Input n k) (state : EvacuationState n)
    (object : Fin n) (reference : Reference n) (h : CopyInvariant input state.copying)
    (rep : Represents input state.copying object reference) :
    ownerUnitsFor weak state reference = weak.ownerUnits object := by
  cases reference with
  | source other => simp [ownerUnitsFor, rep.1]
  | copied index =>
      obtain ⟨record, get, origin⟩ := h.ledger.forwarding_valid object index rep
      simp [ownerUnitsFor, get, origin]

theorem drainWeak_registered (input : CopyInput n) (weak : Input n k)
    (roots scanned : List (Fin n)) (fuel : Nat) (logical : MarkState n) (state : State n k)
    (h : EvacuationInvariant input roots scanned logical state.strong) :
    (drainWeak input weak fuel state).registered = state.registered ++
      (drain input.heap fuel logical).2.flatMap weak.ownerUnits := by
  induction fuel generalizing scanned logical state with
  | zero => simp [drainWeak, drain]
  | succ fuel ih =>
      cases queue : logical.pending with
      | nil =>
          have physical : state.strong.copying.pending = [] := by simp [h.schedule.pending, queue]
          simp [drainWeak, scanCopy, physical, drain, scanOne, queue]
      | cons object rest =>
          obtain ⟨next, step, nextInv, stable⟩ :=
            scan_evacuation input roots scanned logical state.strong object rest h queue
          have oldMarked := (h.marking.accounted object).mpr (Or.inl (by simp [queue]))
          have newMarked := (discoverMany_marked (input.heap.strongChildren object)
            (⟨logical.marked, rest⟩ : MarkState n) object).mpr (Or.inl oldMarked)
          have rep := nextInv.schedule.known object newMarked
          rw [stable object oldMarked] at rep
          have owner := ownerUnitsFor_represents input weak next object _ nextInv.copying rep
          let visited := earlyVisits input { state with strong := next } (weak.ownerUnits object)
          have nextH : EvacuationInvariant input roots (object :: scanned)
              (discoverMany (input.heap.strongChildren object) ⟨logical.marked, rest⟩) visited.strong := by
            simpa [visited] using nextInv
          have tail := ih _ _ visited nextH
          simpa [drainWeak, step, owner, drain, scanOne, queue, visited, List.append_assoc] using tail

theorem drainWeak_lateVisits (input : CopyInput n) (weak : Input n k) (fuel : Nat) (state : State n k) :
    (drainWeak input weak fuel state).lateVisits = state.lateVisits := by
  induction fuel generalizing state with
  | zero => rfl
  | succ fuel ih =>
      cases step : scanCopy input state.strong with
      | none => simp [drainWeak, step]
      | some pair =>
          simpa [drainWeak, step] using ih
            (earlyVisits input { state with strong := pair.2 } (ownerUnitsFor weak pair.2 pair.1))

theorem collection_registered (input : CopyInput n) (weak : Input n k) (roots : List (Fin n)) :
    (collectWeak input weak roots).registered = weak.external ++
      (collect input.heap roots).2.flatMap weak.ownerUnits := by
  have h : EvacuationInvariant input roots [] (seed roots) (start input weak roots).strong := by
    simpa [start] using seed_evacuation input roots
  simpa [collectWeak, finishDeferred, start, collect] using
    drainWeak_registered input weak roots [] n (seed roots) (start input weak roots) h

theorem owners_nodup (weak : Input n k) (wf : WellFormed weak) (objects : List (Fin n))
    (nodup : objects.Nodup) : (objects.flatMap weak.ownerUnits).Nodup := by
  induction objects with
  | nil => simp
  | cons object rest ih =>
      simp only [List.flatMap_cons, List.nodup_append]
      refine ⟨wf.owner_nodup object, ih (List.nodup_cons.mp nodup).2, ?_⟩
      intro left hl right hr eq
      subst right
      obtain ⟨other, member, unitMember⟩ := List.mem_flatMap.mp hr
      have ne : object ≠ other := by
        intro same
        exact (List.nodup_cons.mp nodup).1 (same ▸ member)
      exact wf.owners_disjoint object other ne left hl unitMember

theorem collection_registered_nodup (input : CopyInput n) (weak : Input n k) (roots : List (Fin n))
    (wf : WellFormed weak) : (collectWeak input weak roots).registered.Nodup := by
  rw [collection_registered, List.nodup_append]
  refine ⟨wf.external_nodup, owners_nodup weak wf _ (collect_trace_nodup input.heap roots), ?_⟩
  intro left hl right hr eq
  subst right
  obtain ⟨object, _, member⟩ := List.mem_flatMap.mp hr
  exact wf.external_disjoint object left hl member

/-- Only exact supplied units are visited; no weak fields of unreachable owners
    are silently scanned, and external units do not root their owning objects. -/
theorem collection_registered_iff (input : CopyInput n) (weak : Input n k)
    (roots : List (Fin n)) (id : Fin k) :
    id ∈ (collectWeak input weak roots).registered ↔ id ∈ weak.external ∨
      ∃ object, Reachable input.heap roots object ∧ id ∈ weak.ownerUnits object := by
  simp [collection_registered, List.mem_flatMap, collect_trace_correct]

theorem collection_late_exact (input : CopyInput n) (weak : Input n k) (roots : List (Fin n)) :
    (collectWeak input weak roots).lateVisits =
      (drainWeak input weak n (start input weak roots)).pending := by
  simp [collectWeak, finishDeferred, drainWeak_lateVisits, start]

@[simp] theorem earlyVisit_registered (input : CopyInput n) (state : State n k) (id : Fin k) :
    (earlyVisit input state id).registered = state.registered ++ [id] := rfl

@[simp] theorem earlyVisit_units (input : CopyInput n) (state : State n k) (id other : Fin k) :
    (earlyVisit input state id).units other =
      if other = id then (repairUnit (resolve input state.strong.copying .early) (state.units id)).1
      else state.units other := rfl

@[simp] theorem earlyVisit_pending (input : CopyInput n) (state : State n k) (id : Fin k) :
    (earlyVisit input state id).pending =
      if (repairUnit (resolve input state.strong.copying .early) (state.units id)).2 = .pending
      then state.pending ++ [id] else state.pending := rfl

theorem drainWeak_prefix (input : CopyInput n) (weak : Input n k) (fuel : Nat) (state : State n k) :
    state.registered <+: (drainWeak input weak fuel state).registered := by
  induction fuel generalizing state with
  | zero => simp [drainWeak]
  | succ fuel ih =>
      cases step : scanCopy input state.strong with
      | none => simp [drainWeak, step]
      | some pair =>
          let visited := earlyVisits input { state with strong := pair.2 } (ownerUnitsFor weak pair.2 pair.1)
          have first : state.registered <+: visited.registered := by simp [visited]
          simpa [drainWeak, step, visited] using first.trans (ih visited)

theorem beforeFinish_strong (input : CopyInput n) (weak : Input n k) (roots : List (Fin n)) :
    (drainWeak input weak n (start input weak roots)).strong = (evacuate input roots).1 := by
  simp [drainWeak_strong, start, evacuate]

end CloverGC.Copying.Weak
