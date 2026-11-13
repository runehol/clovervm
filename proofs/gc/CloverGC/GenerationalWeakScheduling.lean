import CloverGC.GenerationalWeakDriverProofs
import CloverGC.GenerationalCorrectness
import CloverGC.WeakScheduling

namespace CloverGC.Generational.Weak

open Copying

@[simp] theorem earlyVisits_registered (input : Input n k) (state : Copying.Weak.State n k)
    (ids : List (Fin k)) : (earlyVisits input state ids).registered = state.registered ++ ids := by
  induction ids generalizing state with
  | nil => simp [earlyVisits]
  | cons id rest ih => simpa [earlyVisits, earlyVisit, List.append_assoc] using ih (earlyVisit input state id)

@[simp] theorem lateVisits_registered (input : Input n k) (state : Copying.Weak.State n k)
    (ids : List (Fin k)) : (lateVisits input state ids).registered = state.registered := by
  induction ids generalizing state with
  | nil => rfl
  | cons id rest ih => exact ih (lateVisit input state id)

@[simp] theorem earlyVisits_lateVisits (input : Input n k) (state : Copying.Weak.State n k)
    (ids : List (Fin k)) : (earlyVisits input state ids).lateVisits = state.lateVisits := by
  induction ids generalizing state with
  | nil => rfl
  | cons id rest ih => exact ih (earlyVisit input state id)

@[simp] theorem rememberOwner_registered (input : Input n k) (state : Copying.Weak.State n k) (owner : Fin n) :
    (rememberOwner input state owner).registered = state.registered ++ input.heap.data.units.ownerUnits owner := by
  simp [rememberOwner]

@[simp] theorem rememberOwners_registered (input : Input n k) (state : Copying.Weak.State n k)
    (owners : List (Fin n)) : (rememberOwners input state owners).registered =
      state.registered ++ owners.flatMap input.heap.data.units.ownerUnits := by
  induction owners generalizing state with
  | nil => simp [rememberOwners]
  | cons owner rest ih => simpa [rememberOwners, List.append_assoc] using ih (rememberOwner input state owner)

@[simp] theorem rememberOwners_lateVisits (input : Input n k) (state : Copying.Weak.State n k)
    (owners : List (Fin n)) : (rememberOwners input state owners).lateVisits = state.lateVisits := by
  induction owners generalizing state with
  | nil => rfl
  | cons owner rest ih => simpa [rememberOwners, rememberOwner] using ih (rememberOwner input state owner)

@[simp] theorem lateVisits_trace (input : Input n k) (state : Copying.Weak.State n k)
    (ids : List (Fin k)) : (lateVisits input state ids).lateVisits = state.lateVisits ++ ids := by
  induction ids generalizing state with
  | nil => simp [lateVisits]
  | cons id rest ih => simpa [lateVisits, lateVisit, List.append_assoc] using ih (lateVisit input state id)

/-- Registration follows logical owner order, while copied references select
    units through their original owner in the allocation ledger. -/
theorem drainWeak_registered (input : Input n k) (done scanned : List (Fin n)) (fuel : Nat)
    (logical : MarkState n) (state : Copying.Weak.State n k)
    (ready : Ready input.strongInput done scanned logical state.strong)
    (marking : Invariant input.strongInput.selectedGraph input.strongInput.logicalRoots scanned logical) :
    (drainWeak input fuel state).1.registered = state.registered ++
      (CloverGC.drain input.strongInput.selectedGraph fuel logical).2.flatMap input.heap.data.units.ownerUnits := by
  induction fuel generalizing scanned logical state with
  | zero => simp [drainWeak, CloverGC.drain]
  | succ fuel ih =>
      cases queue : logical.pending with
      | nil =>
          have physical : state.strong.copying.pending = [] := by simp [ready.tracking.schedule.pending, queue]
          simp [drainWeak, Generational.scan, physical, CloverGC.drain, scanOne, queue]
      | cons object rest =>
          obtain ⟨next, step, nextReady, stable⟩ :=
            ready_scan input.strongInput done scanned logical state.strong object rest ready marking queue
          have oldMarked := (marking.accounted object).mpr (Or.inl (by simp [queue]))
          have newMarked := (discoverMany_marked (input.strongInput.selectedGraph.strongChildren object)
            (⟨logical.marked, rest⟩ : MarkState n) object).mpr (Or.inl oldMarked)
          have rep := nextReady.tracking.schedule.known object newMarked
          rw [stable object (fun _ => oldMarked)] at rep
          have owner := Copying.Weak.ownerUnitsFor_represents input.strongInput.copyInput
            input.heap.data.units next object _ nextReady.tracking.copying rep
          let visited := earlyVisits input { state with strong := next } (input.heap.data.units.ownerUnits object)
          have nextH : Ready input.strongInput done (object :: scanned)
              (discoverMany (input.strongInput.selectedGraph.strongChildren object)
                ⟨logical.marked, rest⟩) visited.strong := by
            simpa [visited] using nextReady
          have tail := ih _ _ visited nextH
            (scan_preserves input.strongInput.selectedGraph input.strongInput.logicalRoots scanned logical
              object rest marking queue)
          simpa [drainWeak, step, owner, CloverGC.drain, scanOne, queue, visited, List.append_assoc] using tail

/-- External storage precedes units enumerated from the shared remembered
    owner list, then units visited while draining selected objects. -/
theorem collection_registered (input : Input n k) :
    (collectWeak input).1.registered = input.heap.data.units.external ++ input.rememberedUnits ++
      (collect input.strongInput.selectedGraph input.strongInput.logicalRoots).2.flatMap
        input.heap.data.units.ownerUnits := by
  have ready : Ready input.strongInput input.strongInput.remembered []
      (seed input.strongInput.logicalRoots) (start input).strong := by
    simpa only [start_strong] using ready_start input.strongInput
  simpa [collectWeak, finishDeferred, start, collect, Input.rememberedUnits, List.append_assoc] using
    drainWeak_registered input input.strongInput.remembered [] n _ _ ready
      (seed_invariant input.strongInput.selectedGraph input.strongInput.logicalRoots)

theorem collection_registered_iff (input : Input n k) (id : Fin k) :
    id ∈ (collectWeak input).1.registered ↔ id ∈ input.heap.data.units.external ∨
      id ∈ input.rememberedUnits ∨ ∃ owner, Live input.strongInput owner ∧
        id ∈ input.heap.data.units.ownerUnits owner := by
  simp [collection_registered, List.mem_flatMap, collect_trace_correct, Live]

theorem collection_registered_nodup (input : Input n k) : (collectWeak input).1.registered.Nodup := by
  have rememberedUnique : input.rememberedUnits.Nodup :=
    Copying.Weak.owners_nodup _ input.heap.valid.storage _ (remembered_nodup input.strongInput)
  have rememberedOld (id : Fin k) (member : id ∈ input.rememberedUnits) :
      ∃ owner, input.strongInput.selected owner = false ∧ id ∈ input.heap.data.units.ownerUnits owner := by
    obtain ⟨owner, remembered, owned⟩ := List.mem_flatMap.mp member
    exact ⟨owner, remembered_unselected input.strongInput owner remembered, owned⟩
  rw [collection_registered, List.append_assoc, List.nodup_append]
  refine ⟨input.heap.valid.storage.external_nodup, ?_, ?_⟩
  · rw [List.nodup_append]
    refine ⟨rememberedUnique, Copying.Weak.owners_nodup _ input.heap.valid.storage _
      (collect_trace_nodup _ _), ?_⟩
    intro left hl right hr same
    subst right
    obtain ⟨owner, outside, owned⟩ := rememberedOld left hl
    obtain ⟨other, scanned, otherOwned⟩ := List.mem_flatMap.mp hr
    have equal := input.heap.owner_unique left owner other owned otherOwned
    have selected := live_selected input.strongInput other ((collect_trace_correct _ _ other).mp scanned)
    simp [equal, selected] at outside
  · intro left external right member same
    subst right
    rcases List.mem_append.mp member with remembered | scanned
    · obtain ⟨owner, _, owned⟩ := rememberedOld left remembered
      exact input.heap.valid.storage.external_disjoint owner left external owned
    · obtain ⟨owner, _, owned⟩ := List.mem_flatMap.mp scanned
      exact input.heap.valid.storage.external_disjoint owner left external owned

theorem drainWeak_lateVisits (input : Input n k) (fuel : Nat) (state : Copying.Weak.State n k) :
    (drainWeak input fuel state).1.lateVisits = state.lateVisits := by
  induction fuel generalizing state with
  | zero => rfl
  | succ fuel ih =>
      cases step : Generational.scan input.strongInput state.strong with
      | none => simp [drainWeak, step]
      | some pair =>
          simpa [drainWeak, step] using ih
            (earlyVisits input { state with strong := pair.2 }
              (Copying.Weak.ownerUnitsFor input.heap.data.units pair.2 pair.1))

theorem collection_late_exact (input : Input n k) :
    (collectWeak input).1.lateVisits = (drainWeak input n (start input)).1.pending := by
  simp [collectWeak, finishDeferred, drainWeak_lateVisits, start]

/-- Pending work is a subsequence of registration history, including its order. -/
theorem earlyVisits_pending_sublist (input : Input n k) (state : Copying.Weak.State n k)
    (ids : List (Fin k)) (sub : state.pending.Sublist state.registered) :
    (earlyVisits input state ids).pending.Sublist (earlyVisits input state ids).registered := by
  induction ids generalizing state with
  | nil => exact sub
  | cons id rest ih =>
      apply ih (earlyVisit input state id)
      change (if (Copying.Weak.repairUnit
        (resolve input.strongInput state.strong.copying .early) (state.units id)).2 = .pending
        then state.pending ++ [id] else state.pending).Sublist (state.registered ++ [id])
      split
      · exact sub.append (List.Sublist.refl [id])
      · exact sub.trans (List.sublist_append_left _ _)

theorem rememberOwners_pending_sublist (input : Input n k) (state : Copying.Weak.State n k)
    (owners : List (Fin n)) (sub : state.pending.Sublist state.registered) :
    (rememberOwners input state owners).pending.Sublist (rememberOwners input state owners).registered := by
  induction owners generalizing state with
  | nil => exact sub
  | cons owner rest ih =>
      apply ih (rememberOwner input state owner)
      exact earlyVisits_pending_sublist input _ _ sub

theorem drainWeak_pending_sublist (input : Input n k) (fuel : Nat) (state : Copying.Weak.State n k)
    (sub : state.pending.Sublist state.registered) :
    (drainWeak input fuel state).1.pending.Sublist (drainWeak input fuel state).1.registered := by
  induction fuel generalizing state with
  | zero => exact sub
  | succ fuel ih =>
      cases step : Generational.scan input.strongInput state.strong with
      | none => simpa [drainWeak, step] using sub
      | some pair =>
          simpa [drainWeak, step] using ih
            (earlyVisits input { state with strong := pair.2 }
              (Copying.Weak.ownerUnitsFor input.heap.data.units pair.2 pair.1))
            (earlyVisits_pending_sublist input { state with strong := pair.2 } _ sub)

theorem collection_late_nodup (input : Input n k) : (collectWeak input).1.lateVisits.Nodup := by
  rw [collection_late_exact]
  have sub : (start input).pending.Sublist (start input).registered := by
    apply rememberOwners_pending_sublist
    exact earlyVisits_pending_sublist input _ _ (List.Sublist.refl [])
  have registered : (drainWeak input n (start input)).1.registered.Nodup := by
    simpa [collectWeak, finishDeferred] using collection_registered_nodup input
  exact List.Nodup.sublist (drainWeak_pending_sublist input n (start input) sub) registered

end CloverGC.Generational.Weak
