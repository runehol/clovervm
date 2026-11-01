import CloverGC.WeakScheduling

namespace CloverGC.Copying.Weak

def desired (input : CopyInput n) (weak : Input n k) (final : CopyState n) (id : Fin k) : Unit n :=
  (repairUnit (resolve input final .late) (weak.templates id).initial).1

/-- A backwards-looking specification relative to the eventual strong result.
    Early-settled units already equal their final repair; deferred units retain
    the same late-repair result as their original template. -/
structure UnitInvariant (input : CopyInput n) (weak : Input n k) (final : CopyState n)
    (state : State n k) : Prop where
  outside : ∀ id, id ∉ state.registered → state.units id = (weak.templates id).initial
  compatible : ∀ id ∈ state.registered,
    repairUnit (resolve input final .late) (state.units id) =
      repairUnit (resolve input final .late) (weak.templates id).initial
  settled : ∀ id ∈ state.registered, id ∉ state.pending → state.units id = desired input weak final id
  pending_registered : ∀ id ∈ state.pending, id ∈ state.registered
  pending_nodup : state.pending.Nodup

theorem earlyVisit_preserves (input : CopyInput n) (weak : Input n k) (final : CopyState n)
    (state : State n k) (id : Fin k) (invariant : UnitInvariant input weak final state)
    (progress : Progress state.strong.copying final) (fresh : id ∉ state.registered) :
    UnitInvariant input weak final (earlyVisit input state id) := by
  have originalUnit := invariant.outside id fresh
  have not_pending : id ∉ state.pending := fun hp => fresh (invariant.pending_registered id hp)
  refine {
    outside := ?_
    compatible := ?_
    settled := ?_
    pending_registered := ?_
    pending_nodup := ?_
  }
  · intro other not_registered
    have old : other ∉ state.registered := by simpa using fun member => not_registered (by simp [member])
    have ne : other ≠ id := by intro eq; subst other; exact not_registered (by simp)
    simpa [ne] using invariant.outside other old
  · intro other registered
    rcases List.mem_append.mp registered with old | new
    · have ne : other ≠ id := by intro eq; subst other; exact fresh old
      simpa [ne] using invariant.compatible other old
    · have eq : other = id := by simpa using new
      subst other
      simpa [originalUnit] using early_unit_compatible input state.strong.copying final progress (weak.templates id).initial
  · intro other registered not_pending_new
    rcases List.mem_append.mp registered with old | new
    · have ne : other ≠ id := by intro eq; subst other; exact fresh old
      have old_not_pending : other ∉ state.pending := by
        intro member
        apply not_pending_new
        simp only [earlyVisit_pending]
        split <;> simp [member]
      simpa [ne] using invariant.settled other old old_not_pending
    · have eq : other = id := by simpa using new
      subst other
      have complete : (repairUnit (resolve input state.strong.copying .early) (state.units id)).2 = .complete := by
        cases status : (repairUnit (resolve input state.strong.copying .early) (state.units id)).2 with
        | complete => rfl
        | pending => simp [status] at not_pending_new
      simpa [originalUnit, desired] using early_complete_correct input state.strong.copying final
        progress (weak.templates id).initial (by simpa [originalUnit] using complete)
  · intro other member
    simp only [earlyVisit_pending] at member
    split at member
    · rcases List.mem_append.mp member with old | new
      · exact List.mem_append.mpr (Or.inl (invariant.pending_registered other old))
      · exact List.mem_append.mpr (Or.inr new)
    · exact List.mem_append.mpr (Or.inl (invariant.pending_registered other member))
  · simp only [earlyVisit_pending]
    split
    · simp only [List.nodup_append, List.nodup_cons, List.not_mem_nil, List.nodup_nil,
        not_false_eq_true, and_self]
      exact ⟨invariant.pending_nodup, by grind⟩
    · exact invariant.pending_nodup

theorem earlyVisits_preserves (input : CopyInput n) (weak : Input n k) (final : CopyState n)
    (state : State n k) (ids : List (Fin k)) (h : UnitInvariant input weak final state)
    (progress : Progress state.strong.copying final)
    (nodup : (state.registered ++ ids).Nodup) :
    UnitInvariant input weak final (earlyVisits input state ids) := by
  induction ids generalizing state with
  | nil => exact h
  | cons id rest ih =>
      have head_nodup : (state.registered ++ [id]).Nodup :=
        List.Nodup.sublist (List.prefix_append _ rest).sublist (by simpa [List.append_assoc] using nodup)
      have fresh : id ∉ state.registered := by
        have disjoint := (List.nodup_append.mp head_nodup).2.2
        intro member
        exact disjoint id member id (by simp) rfl
      apply ih (earlyVisit input state id) (earlyVisit_preserves input weak final state id h progress fresh)
      · exact progress
      · simpa [List.append_assoc] using nodup

theorem drainWeak_units (input : CopyInput n) (weak : Input n k) (final : CopyState n)
    (fuel : Nat) (state : State n k) (unitInvariant : UnitInvariant input weak final state)
    (end_state : final = (drainCopies input fuel state.strong).1.copying)
    (nodup : (drainWeak input weak fuel state).registered.Nodup) :
    UnitInvariant input weak final (drainWeak input weak fuel state) := by
  induction fuel generalizing state with
  | zero => exact unitInvariant
  | succ fuel inductionHypothesis =>
      cases step : scanCopy input state.strong with
      | none => simpa [drainWeak, step] using unitInvariant
      | some pair =>
          let prepared := { state with strong := pair.2 }
          let ids := ownerUnitsFor weak pair.2 pair.1
          let visited := earlyVisits input prepared ids
          have tail_end : final = (drainCopies input fuel pair.2).1.copying := by
            simpa [drainCopies, step] using end_state
          have progress : Progress pair.2.copying final := by
            rw [tail_end]
            exact drain_progress input fuel pair.2
          have preparedInvariant : UnitInvariant input weak final prepared :=
            ⟨unitInvariant.outside, unitInvariant.compatible, unitInvariant.settled, unitInvariant.pending_registered, unitInvariant.pending_nodup⟩
          have finalRegistrationNodup : (drainWeak input weak fuel visited).registered.Nodup := by
            simpa [drainWeak, step, visited, prepared, ids] using nodup
          have visited_nodup := List.Nodup.sublist (drainWeak_prefix input weak fuel visited).sublist finalRegistrationNodup
          have registrationNodup : (prepared.registered ++ ids).Nodup := by simpa [visited] using visited_nodup
          have visitedInvariant := earlyVisits_preserves input weak final prepared ids preparedInvariant progress registrationNodup
          simpa [drainWeak, step, visited, prepared, ids] using
            inductionHypothesis visited visitedInvariant (by simpa [visited, prepared] using tail_end) finalRegistrationNodup

theorem beforeFinish_units (input : CopyInput n) (weak : Input n k) (roots : List (Fin n))
    (wf : WellFormed weak) :
    UnitInvariant input weak (evacuate input roots).1.copying
      (drainWeak input weak n (start input weak roots)) := by
  let initial : State n k :=
    ⟨seedRoots input roots, fun id => (weak.templates id).initial, [], [], []⟩
  have h : UnitInvariant input weak (evacuate input roots).1.copying initial := by
    exact {
      outside := by simp [initial]
      compatible := by simp [initial]
      settled := by simp [initial]
      pending_registered := by simp [initial]
      pending_nodup := by simp [initial]
    }
  have progress : Progress initial.strong.copying (evacuate input roots).1.copying :=
    drain_progress input n (seedRoots input roots)
  have seeded := earlyVisits_preserves input weak (evacuate input roots).1.copying initial
    weak.external h progress (by simpa [initial] using wf.external_nodup)
  apply drainWeak_units input weak (evacuate input roots).1.copying n (start input weak roots) seeded
  · simp [start, evacuate]
  · simpa [collectWeak, finishDeferred] using collection_registered_nodup input weak roots wf

theorem lateVisits_unmentioned (input : CopyInput n) (state : State n k) (ids : List (Fin k))
    (id : Fin k) (not_visited : id ∉ ids) :
    (lateVisits input state ids).units id = state.units id := by
  induction ids generalizing state with
  | nil => rfl
  | cons head rest ih =>
      have ne : id ≠ head := fun eq => not_visited (by simp [eq])
      have not_rest : id ∉ rest := fun member => not_visited (by simp [member])
      simpa [lateVisits, lateVisit, ne] using ih (lateVisit input state head) not_rest

theorem lateVisits_units (input : CopyInput n) (state : State n k) (ids : List (Fin k))
    (nodup : ids.Nodup) (id : Fin k) :
    (lateVisits input state ids).units id =
      if id ∈ ids then (repairUnit (resolve input state.strong.copying .late) (state.units id)).1
      else state.units id := by
  induction ids generalizing state with
  | nil => simp [lateVisits]
  | cons head rest ih =>
      by_cases same : id = head
      · subst id
        have no : head ∉ rest := (List.nodup_cons.mp nodup).1
        have untouched := lateVisits_unmentioned input (lateVisit input state head) rest head no
        simpa [lateVisits, lateVisit] using untouched
      · simpa [lateVisits, lateVisit, same] using
          ih (lateVisit input state head) (List.nodup_cons.mp nodup).2

/-- Every supplied surviving unit equals a direct late repair of its original
    template, despite early partial writes and skipping already-settled units. -/
theorem collection_units_correct (input : CopyInput n) (weak : Input n k)
    (roots : List (Fin n)) (wf : WellFormed weak) (id : Fin k)
    (registered : id ∈ (collectWeak input weak roots).registered) :
    (collectWeak input weak roots).units id = desired input weak (evacuate input roots).1.copying id := by
  let before := drainWeak input weak n (start input weak roots)
  have h := beforeFinish_units input weak roots wf
  have strong : before.strong.copying = (evacuate input roots).1.copying :=
    congrArg EvacuationState.copying (beforeFinish_strong input weak roots)
  have member : id ∈ before.registered := by
    simpa [collectWeak, finishDeferred, before] using registered
  have lookup := lateVisits_units input before before.pending h.pending_nodup id
  change (lateVisits input before before.pending).units id = _
  rw [lookup]
  by_cases pending : id ∈ before.pending
  · simp only [pending, ↓reduceIte]
    rw [strong]
    exact congrArg Prod.fst (h.compatible id member)
  · simp only [pending, ↓reduceIte]
    exact h.settled id member pending

theorem collection_late_nodup (input : CopyInput n) (weak : Input n k) (roots : List (Fin n))
    (wf : WellFormed weak) : (collectWeak input weak roots).lateVisits.Nodup := by
  rw [collection_late_exact]
  exact (beforeFinish_units input weak roots wf).pending_nodup

theorem collection_weak_queue_empty (input : CopyInput n) (weak : Input n k) (roots : List (Fin n)) :
    (collectWeak input weak roots).pending = [] := rfl

theorem earlyVisit_deferred_iff (input : CopyInput n) (state : State n k) (id : Fin k)
    (fresh : id ∉ state.pending) :
    id ∈ (earlyVisit input state id).pending ↔
      (repairUnit (resolve input state.strong.copying .early) (state.units id)).2 = .pending := by
  rw [earlyVisit_pending]
  split <;> simp_all

theorem collection_live_unit (input : CopyInput n) (weak : Input n k) (roots : List (Fin n))
    (wf : WellFormed weak) (id : Fin k) (registered : id ∈ (collectWeak input weak roots).registered)
    (all_live : ∀ object, some object ∈ (weak.templates id).targets → Reachable input.heap roots object) :
    (collectWeak input weak roots).units id =
      ⟨(weak.templates id).targets.map (Option.map (canonical (evacuate input roots).1.copying)),
        (weak.templates id).payload⟩ := by
  rw [collection_units_correct input weak roots wf id registered]
  exact repair_live_template input roots (weak.templates id) all_live

/-- Dead dominates Pending: one dead target clears every field and the payload,
    including otherwise-live fields in the same coordinated unit. -/
theorem collection_dead_unit (input : CopyInput n) (weak : Input n k) (roots : List (Fin n))
    (wf : WellFormed weak) (id : Fin k) (registered : id ∈ (collectWeak input weak roots).registered)
    (object : Fin n) (member : some object ∈ (weak.templates id).targets)
    (dead : ¬ Reachable input.heap roots object) :
    (collectWeak input weak roots).units id = clear (weak.templates id).initial := by
  rw [collection_units_correct input weak roots wf id registered]
  exact congrArg Prod.fst (repair_dead_template input roots (weak.templates id) object member dead)

theorem collection_disposal_safe (input : CopyInput n) (weak : Input n k) (roots : List (Fin n))
    (wf : WellFormed weak) :
    ∀ id ∈ (collectWeak input weak roots).registered,
      ∀ reference, some reference ∈ ((collectWeak input weak roots).units id).targets →
        Canonical input (collectWeak input weak roots).strong.copying reference := by
  intro id registered reference member
  rw [collection_units_correct input weak roots wf id registered] at member
  rw [collectWeak_strong]
  exact repair_template_safe input _ (weak.templates id) (evacuate_invariant input roots).copying reference member

end CloverGC.Copying.Weak
