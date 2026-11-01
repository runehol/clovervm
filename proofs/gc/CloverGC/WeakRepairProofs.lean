import CloverGC.WeakRepair
import CloverGC.EvacuationProofs

namespace CloverGC.Copying.Weak

theorem resolve_early_not_dead (input : CopyInput n) (copying : CopyState n) (reference : Reference n) :
    resolve input copying .early reference ≠ .dead := by
  cases reference with
  | copied _ => simp [resolve]
  | source object =>
      cases movable : input.movable object <;>
        simp only [resolve, movable, Bool.false_eq_true, ↓reduceIte]
      · split <;> simp [unresolved]
      · cases copying.forwarding object <;> simp [unresolved]

theorem resolve_late_not_pending (input : CopyInput n) (copying : CopyState n) (reference : Reference n) :
    resolve input copying .late reference ≠ .pending := by
  cases reference with
  | copied _ => simp [resolve]
  | source object =>
      cases movable : input.movable object <;>
        simp only [resolve, movable, Bool.false_eq_true, ↓reduceIte]
      · split <;> simp [unresolved]
      · cases copying.forwarding object <;> simp [unresolved]

theorem repair_late_complete (input : CopyInput n) (copying : CopyState n) (unit : Unit n) :
    (repairUnit (resolve input copying .late) unit).2 = .complete := by
  have no_pending : ∀ field, (repairField (resolve input copying .late) field).pending = false := by
    intro field
    cases field with
    | none => rfl
    | some reference =>
        have ne := resolve_late_not_pending input copying reference
        cases result : resolve input copying .late reference <;> simp_all [repairField]
  simp [repairUnit, List.any_map, no_pending]
  split <;> rfl

theorem repair_early_payload (input : CopyInput n) (copying : CopyState n) (unit : Unit n) :
    (repairUnit (resolve input copying .early) unit).1.payload = unit.payload := by
  have no_dead : ∀ field, (repairField (resolve input copying .early) field).dead = false := by
    intro field
    cases field with
    | none => rfl
    | some reference =>
        have ne := resolve_early_not_dead input copying reference
        cases result : resolve input copying .early reference <;> simp_all [repairField]
  simp [repairUnit, List.any_map, no_dead]

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

/-- Collection progress preserves earlier liveness and forwarding decisions. -/
structure Progress (before after : CopyState n) : Prop where
  forwarding : ∀ object index, before.forwarding object = some index → after.forwarding object = some index
  marked : ∀ reference ∈ before.marked, reference ∈ after.marked

theorem Progress.refl (state : CopyState n) : Progress state state :=
  ⟨by intro o i h; exact h, by intro r h; exact h⟩

theorem Progress.trans (a b c : CopyState n) (ab : Progress a b) (bc : Progress b c) : Progress a c :=
  ⟨by intro o i h; exact bc.forwarding o i (ab.forwarding o i h),
    by intro r h; exact bc.marked r (ab.marked r h)⟩

theorem discovery_progress (input : CopyInput n) (state : CopyState n) (reference : Reference n) :
    Progress state (discoverReference input state reference).1 := by
  constructor
  · exact discovery_preserves_forwarding input state reference
  · intro r member
    cases reference with
    | copied _ => exact member
    | source object =>
        cases movable : input.movable object with
        | false => simpa [discoverReference, movable, enqueue_marked] using Or.inr member
        | true =>
            cases forward : state.forwarding object <;>
              simp [discoverReference, movable, forward, enqueue_marked, installCopy, member]

theorem repair_progress (input : CopyInput n) (state : CopyState n) (references : List (Reference n)) :
    Progress state (repairReferences input state references).1 := by
  induction references generalizing state with
  | nil => exact Progress.refl state
  | cons reference rest ih =>
      exact Progress.trans _ _ _ (discovery_progress input state reference)
        (ih (discoverReference input state reference).1)

theorem scan_progress (input : CopyInput n) (state next : EvacuationState n) (reference : Reference n)
    (step : scanCopy input state = some (reference, next)) : Progress state.copying next.copying := by
  cases queue : state.copying.pending with
  | nil => simp [scanCopy, queue] at step
  | cons head rest =>
      let popped : CopyState n := { state.copying with pending := rest }
      let repaired := repairReferences input popped (liveFields state head)
      have eq : (head, writeFields { state with copying := repaired.1 } head repaired.2) =
          (reference, next) := by simpa [scanCopy, queue] using step
      obtain ⟨rfl, rfl⟩ := Prod.mk.inj eq
      have progress := repair_progress input popped (liveFields state head)
      constructor
      · simpa using progress.forwarding
      · simpa using progress.marked

theorem drain_progress (input : CopyInput n) (fuel : Nat) (state : EvacuationState n) :
    Progress state.copying (drainCopies input fuel state).1.copying := by
  induction fuel generalizing state with
  | zero => exact Progress.refl state.copying
  | succ fuel ih =>
      cases step : scanCopy input state with
      | none => simpa [drainCopies, step] using Progress.refl state.copying
      | some pair =>
          simpa [drainCopies, step] using
            Progress.trans _ _ _ (scan_progress input state pair.2 pair.1 step) (ih pair.2)

theorem resolve_stable (input : CopyInput n) (before after : CopyState n)
    (progress : Progress before after) (reference target : Reference n)
    (resolved : resolve input before .early reference = .resolved target) :
    resolve input after .late reference = .resolved target ∧
      resolve input after .late target = .resolved target := by
  cases reference with
  | copied index =>
      have same : Reference.copied index = target := by simpa [resolve] using resolved
      subst target
      simp [resolve]
  | source object =>
      cases movable : input.movable object with
      | false =>
          by_cases member : Reference.source object ∈ before.marked
          · have same : Reference.source object = target := by simpa [resolve, movable, member] using resolved
            subst target
            simp [resolve, movable, progress.marked _ member]
          · simp [resolve, movable, member, unresolved] at resolved
      | true =>
          cases forward : before.forwarding object with
          | none => simp [resolve, movable, forward, unresolved] at resolved
          | some index =>
              have same : Reference.copied index = target := by simpa [resolve, movable, forward] using resolved
              subst target
              simp [resolve, movable, progress.forwarding _ _ forward]

theorem early_field_dead_false (input : CopyInput n) (state : CopyState n) (field : Option (Reference n)) :
    (repairField (resolve input state .early) field).dead = false := by
  cases field with
  | none => rfl
  | some reference =>
      have ne := resolve_early_not_dead input state reference
      cases result : resolve input state .early reference <;> simp_all [repairField]

theorem late_field_pending_false (input : CopyInput n) (state : CopyState n) (field : Option (Reference n)) :
    (repairField (resolve input state .late) field).pending = false := by
  cases field with
  | none => rfl
  | some reference =>
      have ne := resolve_late_not_pending input state reference
      cases result : resolve input state .late reference <;> simp_all [repairField]

/-- An early rewrite cannot change the result of eventual late resolution. -/
theorem early_field_compatible (input : CopyInput n) (before after : CopyState n)
    (progress : Progress before after) (field : Option (Reference n)) :
    repairField (resolve input after .late)
        (repairField (resolve input before .early) field).target =
      repairField (resolve input after .late) field := by
  cases field with
  | none => rfl
  | some reference =>
      cases early : resolve input before .early reference with
      | dead => exact False.elim (resolve_early_not_dead input before reference early)
      | pending => simp [repairField, early]
      | resolved target =>
          obtain ⟨late, stable⟩ := resolve_stable input before after progress reference target early
          simp [repairField, early, late, stable]

theorem early_field_settled (input : CopyInput n) (before after : CopyState n)
    (progress : Progress before after) (field : Option (Reference n))
    (settled : (repairField (resolve input before .early) field).pending = false) :
    repairField (resolve input after .late) field =
      ⟨(repairField (resolve input before .early) field).target, false, false⟩ := by
  cases field with
  | none => rfl
  | some reference =>
      cases early : resolve input before .early reference with
      | dead => exact False.elim (resolve_early_not_dead input before reference early)
      | pending => simp [repairField, early] at settled
      | resolved target =>
          simp [repairField, early, (resolve_stable input before after progress reference target early).1]

theorem repair_early_form (input : CopyInput n) (state : CopyState n) (unit : Unit n) :
    repairUnit (resolve input state .early) unit =
      (⟨unit.targets.map (fun field => (repairField (resolve input state .early) field).target), unit.payload⟩,
       if (unit.targets.map (repairField (resolve input state .early))).any FieldResult.pending
       then .pending else .complete) := by
  simp [repairUnit, List.any_map, early_field_dead_false, List.map_map, Function.comp_def]

theorem early_unit_compatible (input : CopyInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n) :
    repairUnit (resolve input after .late) (repairUnit (resolve input before .early) unit).1 =
      repairUnit (resolve input after .late) unit := by
  rw [repair_early_form]
  simp only [repairUnit, List.map_map, Function.comp_def]
  have same : unit.targets.map (fun field => repairField (resolve input after .late)
      (repairField (resolve input before .early) field).target) =
      unit.targets.map (repairField (resolve input after .late)) :=
    List.map_congr_left (fun field _ => early_field_compatible input before after progress field)
  rw [same]
  split
  · simp [clear, List.map_map, Function.comp_def]
  · have targets := congrArg (List.map FieldResult.target) same
    simp only [List.map_map, Function.comp_def] at targets
    rw [targets]

theorem early_complete_correct (input : CopyInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n)
    (complete : (repairUnit (resolve input before .early) unit).2 = .complete) :
    (repairUnit (resolve input before .early) unit).1 =
      (repairUnit (resolve input after .late) unit).1 := by
  have no_pending : ∀ field ∈ unit.targets,
      (repairField (resolve input before .early) field).pending = false := by
    rw [repair_early_form] at complete
    split at complete
    · contradiction
    · rename_i no
      simpa [List.any_map, List.any_eq_false] using no
  have facts (field) (member : field ∈ unit.targets) :=
    early_field_settled input before after progress field (no_pending field member)
  have dead : (unit.targets.map (repairField (resolve input after .late))).any FieldResult.dead = false := by
    simp only [List.any_eq_false, List.mem_map]
    rintro result ⟨field, member, rfl⟩
    simp [facts field member]
  rw [repair_early_form]
  simp only [repairUnit, dead, Bool.false_eq_true, ↓reduceIte]
  congr 1
  simp only [List.map_map, Function.comp_def]
  exact List.map_congr_left (fun field member =>
    (congrArg FieldResult.target (facts field member)).symm)

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
    (state : State n k) (id : Fin k) (h : UnitInvariant input weak final state)
    (progress : Progress state.strong.copying final) (fresh : id ∉ state.registered) :
    UnitInvariant input weak final (earlyVisit input state id) := by
  have original := h.outside id fresh
  have not_pending : id ∉ state.pending := fun hp => fresh (h.pending_registered id hp)
  constructor
  · intro other not_registered
    have old : other ∉ state.registered := by simpa using fun member => not_registered (by simp [member])
    have ne : other ≠ id := by intro eq; subst other; exact not_registered (by simp)
    simpa [ne] using h.outside other old
  · intro other registered
    rcases List.mem_append.mp registered with old | new
    · have ne : other ≠ id := by intro eq; subst other; exact fresh old
      simpa [ne] using h.compatible other old
    · have eq : other = id := by simpa using new
      subst other
      simpa [original] using early_unit_compatible input state.strong.copying final progress (weak.templates id).initial
  · intro other registered not_pending_new
    rcases List.mem_append.mp registered with old | new
    · have ne : other ≠ id := by intro eq; subst other; exact fresh old
      have old_not_pending : other ∉ state.pending := by
        intro member
        apply not_pending_new
        simp only [earlyVisit_pending]
        split <;> simp [member]
      simpa [ne] using h.settled other old old_not_pending
    · have eq : other = id := by simpa using new
      subst other
      have complete : (repairUnit (resolve input state.strong.copying .early) (state.units id)).2 = .complete := by
        cases status : (repairUnit (resolve input state.strong.copying .early) (state.units id)).2 with
        | complete => rfl
        | pending => simp [status] at not_pending_new
      simpa [original, desired] using early_complete_correct input state.strong.copying final
        progress (weak.templates id).initial (by simpa [original] using complete)
  · intro other member
    simp only [earlyVisit_pending] at member
    split at member
    · rcases List.mem_append.mp member with old | new
      · exact List.mem_append.mpr (Or.inl (h.pending_registered other old))
      · exact List.mem_append.mpr (Or.inr new)
    · exact List.mem_append.mpr (Or.inl (h.pending_registered other member))
  · simp only [earlyVisit_pending]
    split
    · simp only [List.nodup_append, List.nodup_cons, List.not_mem_nil, List.nodup_nil,
        not_false_eq_true, and_self]
      exact ⟨h.pending_nodup, by grind⟩
    · exact h.pending_nodup

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

theorem drainWeak_units (input : CopyInput n) (weak : Input n k) (final : CopyState n)
    (fuel : Nat) (state : State n k) (h : UnitInvariant input weak final state)
    (end_state : final = (drainCopies input fuel state.strong).1.copying)
    (nodup : (drainWeak input weak fuel state).registered.Nodup) :
    UnitInvariant input weak final (drainWeak input weak fuel state) := by
  induction fuel generalizing state with
  | zero => exact h
  | succ fuel ih =>
      cases step : scanCopy input state.strong with
      | none => simpa [drainWeak, step] using h
      | some pair =>
          let prepared := { state with strong := pair.2 }
          let ids := ownerUnitsFor weak pair.2 pair.1
          let visited := earlyVisits input prepared ids
          have tail_end : final = (drainCopies input fuel pair.2).1.copying := by
            simpa [drainCopies, step] using end_state
          have progress : Progress pair.2.copying final := by
            rw [tail_end]
            exact drain_progress input fuel pair.2
          have hprepared : UnitInvariant input weak final prepared :=
            ⟨h.outside, h.compatible, h.settled, h.pending_registered, h.pending_nodup⟩
          have all : (drainWeak input weak fuel visited).registered.Nodup := by
            simpa [drainWeak, step, visited, prepared, ids] using nodup
          have visited_nodup := List.Nodup.sublist (drainWeak_prefix input weak fuel visited).sublist all
          have before : (prepared.registered ++ ids).Nodup := by simpa [visited] using visited_nodup
          have hvisited := earlyVisits_preserves input weak final prepared ids hprepared progress before
          simpa [drainWeak, step, visited, prepared, ids] using
            ih visited hvisited (by simpa [visited, prepared] using tail_end) all

theorem beforeFinish_strong (input : CopyInput n) (weak : Input n k) (roots : List (Fin n)) :
    (drainWeak input weak n (start input weak roots)).strong = (evacuate input roots).1 := by
  simp [drainWeak_strong, start, evacuate]

theorem beforeFinish_units (input : CopyInput n) (weak : Input n k) (roots : List (Fin n))
    (wf : WellFormed weak) :
    UnitInvariant input weak (evacuate input roots).1.copying
      (drainWeak input weak n (start input weak roots)) := by
  let initial : State n k :=
    ⟨seedRoots input roots, fun id => (weak.templates id).initial, [], [], []⟩
  have h : UnitInvariant input weak (evacuate input roots).1.copying initial := by
    constructor <;> simp [initial]
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

theorem resolve_final_live (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (reachable : Reachable input.heap roots object) :
    resolve input (evacuate input roots).1.copying .late (.source object) =
      .resolved (canonical (evacuate input roots).1.copying object) := by
  cases movable : input.movable object with
  | false =>
      have fixed := evacuate_nonmoving input roots object movable
      have marked := (evacuate_reachable input roots object).mpr reachable
      rw [fixed] at marked
      simp [resolve, movable, marked, fixed]
  | true =>
      obtain ⟨index, forward⟩ := (evacuate_forwarded_iff input roots object).mpr ⟨reachable, movable⟩
      simp [resolve, movable, forward, canonical]

theorem resolve_final_dead (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (unreachable : ¬ Reachable input.heap roots object) :
    resolve input (evacuate input roots).1.copying .late (.source object) = .dead := by
  cases movable : input.movable object with
  | false =>
      have fixed := evacuate_nonmoving input roots object movable
      have unmarked : Reference.source object ∉ (evacuate input roots).1.copying.marked := by
        intro member
        exact unreachable ((evacuate_reachable input roots object).mp (fixed ▸ member))
      simp [resolve, movable, unmarked, unresolved]
  | true =>
      have unforwarded : (evacuate input roots).1.copying.forwarding object = none := by
        cases forward : (evacuate input roots).1.copying.forwarding object with
        | none => rfl
        | some index => exact False.elim (unreachable ((evacuate_forwarded_iff input roots object).mp ⟨index, forward⟩).1)
      simp [resolve, movable, unforwarded, unresolved]

theorem resolved_source_canonical (input : CopyInput n) (copying : CopyState n)
    (object : Fin n) (target : Reference n) (h : CopyInvariant input copying)
    (resolved : resolve input copying .late (.source object) = .resolved target) :
    Canonical input copying target := by
  cases movable : input.movable object with
  | false =>
      by_cases marked : Reference.source object ∈ copying.marked
      · have same : Reference.source object = target := by simpa [resolve, movable, marked] using resolved
        subst target
        exact movable
      · simp [resolve, movable, marked, unresolved] at resolved
  | true =>
      cases forward : copying.forwarding object with
      | none => simp [resolve, movable, forward, unresolved] at resolved
      | some index =>
          have same : Reference.copied index = target := by simpa [resolve, movable, forward] using resolved
          subst target
          obtain ⟨record, get, _⟩ := h.ledger.forwarding_valid object index forward
          exact ⟨record, get⟩

theorem repairUnit_target (resolver : Reference n → Resolution n) (unit : Unit n)
    (reference : Reference n) (member : some reference ∈ (repairUnit resolver unit).1.targets) :
    ∃ field ∈ unit.targets, (repairField resolver field).target = some reference := by
  simp only [repairUnit] at member
  split at member
  · simp [clear] at member
  · obtain ⟨result, inResults, eq⟩ := List.mem_map.mp member
    obtain ⟨field, inFields, rfl⟩ := List.mem_map.mp inResults
    exact ⟨field, inFields, eq⟩

theorem repair_template_safe (input : CopyInput n) (copying : CopyState n) (template : Template n)
    (h : CopyInvariant input copying) (reference : Reference n)
    (member : some reference ∈ (repairUnit (resolve input copying .late) template.initial).1.targets) :
    Canonical input copying reference := by
  obtain ⟨field, inFields, eq⟩ := repairUnit_target _ _ reference member
  obtain ⟨original, _, rfl⟩ := List.mem_map.mp inFields
  cases original with
  | none => simp [repairField] at eq
  | some object =>
      cases resolved : resolve input copying .late (.source object) with
      | dead => simp [repairField, resolved] at eq
      | pending => exact False.elim (resolve_late_not_pending input copying (.source object) resolved)
      | resolved target =>
          have same : target = reference := by simpa [repairField, resolved] using eq
          subst reference
          exact resolved_source_canonical input copying object target h resolved

theorem repair_live_template (input : CopyInput n) (roots : List (Fin n)) (template : Template n)
    (all_live : ∀ object, some object ∈ template.targets → Reachable input.heap roots object) :
    (repairUnit (resolve input (evacuate input roots).1.copying .late) template.initial).1 =
      ⟨template.targets.map (Option.map (canonical (evacuate input roots).1.copying)), template.payload⟩ := by
  let resolver := resolve input (evacuate input roots).1.copying .late
  have field (original : Option (Fin n)) (member : original ∈ template.targets) :
      repairField resolver (original.map Reference.source) =
        ⟨original.map (canonical (evacuate input roots).1.copying), false, false⟩ := by
    cases original with
    | none => rfl
    | some object => simp [resolver, repairField, resolve_final_live input roots object (all_live object member)]
  have no_dead : (template.initial.targets.map (repairField resolver)).any FieldResult.dead = false := by
    simp only [List.any_eq_false, List.mem_map]
    rintro result ⟨target, inTargets, rfl⟩
    obtain ⟨original, inOriginal, rfl⟩ := List.mem_map.mp inTargets
    simp [field original inOriginal]
  change (repairUnit resolver template.initial).1 = _
  simp only [repairUnit, no_dead, Bool.false_eq_true, ↓reduceIte]
  congr 1
  simp only [Template.initial, List.map_map, Function.comp_def]
  exact List.map_congr_left (fun original member => congrArg FieldResult.target (field original member))

theorem repair_dead_dominates (resolver : Reference n → Resolution n) (unit : Unit n)
    (reference : Reference n) (member : some reference ∈ unit.targets)
    (dead : resolver reference = .dead) :
    repairUnit resolver unit = (clear unit, .complete) := by
  have has_dead : (unit.targets.map (repairField resolver)).any FieldResult.dead = true := by
    apply List.any_eq_true.mpr
    refine ⟨repairField resolver (some reference), List.mem_map.mpr ⟨some reference, member, rfl⟩, ?_⟩
    simp [repairField, dead]
  simp [repairUnit, has_dead]

theorem repair_dead_template (input : CopyInput n) (roots : List (Fin n)) (template : Template n)
    (object : Fin n) (member : some object ∈ template.targets)
    (dead : ¬ Reachable input.heap roots object) :
    repairUnit (resolve input (evacuate input roots).1.copying .late) template.initial =
      (clear template.initial, .complete) :=
  repair_dead_dominates _ template.initial (.source object)
    (List.mem_map.mpr ⟨some object, member, rfl⟩) (resolve_final_dead input roots object dead)

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
