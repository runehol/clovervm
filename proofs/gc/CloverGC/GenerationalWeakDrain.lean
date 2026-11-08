import CloverGC.GenerationalProgress
import CloverGC.GenerationalWeakRepairInvariant

namespace CloverGC.Generational.Weak

open Copying

theorem drainWeak_prefix (input : Input n k) (fuel : Nat) (state : Copying.Weak.State n k) :
    state.registered <+: (drainWeak input fuel state).1.registered := by
  induction fuel generalizing state with
  | zero => simp [drainWeak]
  | succ fuel ih =>
      cases step : Generational.scan input.strongInput state.strong with
      | none => simp [drainWeak, step]
      | some pair =>
          let visited := earlyVisits input { state with strong := pair.2 }
            (Copying.Weak.ownerUnitsFor input.heap.data.units pair.2 pair.1)
          have first : state.registered <+: visited.registered := by simp [visited]
          simpa [drainWeak, step, visited] using first.trans (ih visited)

/-- The invariant survives any bounded drain, relative to that drain's strong
    result. Final registration uniqueness supplies freshness at every scan. -/
theorem drainWeak_units (input : Input n k) (final : CopyState n) (fuel : Nat)
    (state : Copying.Weak.State n k) (invariant : UnitInvariant input final state)
    (end_state : final = (Generational.drain input.strongInput fuel state.strong).1.copying)
    (nodup : (drainWeak input fuel state).1.registered.Nodup) :
    UnitInvariant input final (drainWeak input fuel state).1 := by
  induction fuel generalizing state with
  | zero => exact invariant
  | succ fuel ih =>
      cases step : Generational.scan input.strongInput state.strong with
      | none => simpa [drainWeak, step] using invariant
      | some pair =>
          let prepared := { state with strong := pair.2 }
          let ids := Copying.Weak.ownerUnitsFor input.heap.data.units pair.2 pair.1
          let visited := earlyVisits input prepared ids
          have tail_end : final = (Generational.drain input.strongInput fuel pair.2).1.copying := by
            simpa [Generational.drain, step] using end_state
          have progress : Progress pair.2.copying final := by
            rw [tail_end]
            exact Generational.drain_progress input.strongInput fuel pair.2
          have preparedInvariant : UnitInvariant input final prepared :=
            ⟨invariant.outside, invariant.compatible, invariant.settled⟩
          have finalNodup : (drainWeak input fuel visited).1.registered.Nodup := by
            simpa [drainWeak, step, visited, prepared, ids] using nodup
          have visitedNodup := List.Nodup.sublist (drainWeak_prefix input fuel visited).sublist finalNodup
          have registrationNodup : (prepared.registered ++ ids).Nodup := by simpa [visited] using visitedNodup
          have visitedInvariant := earlyVisits_preserves input final prepared ids preparedInvariant progress registrationNodup
          simpa [drainWeak, step, visited, prepared, ids] using
            ih visited visitedInvariant (by simpa [visited, prepared] using tail_end) finalNodup

theorem rememberOwners_units (input : Input n k) (final : CopyState n) (state : Copying.Weak.State n k)
    (owners : List (Fin n)) (invariant : UnitInvariant input final state)
    (progress : Progress (Generational.remember input.strongInput state.strong owners).copying final)
    (nodup : (rememberOwners input state owners).registered.Nodup) :
    UnitInvariant input final (rememberOwners input state owners) := by
  induction owners generalizing state with
  | nil => exact invariant
  | cons owner rest ih =>
      let result := Generational.repair input.strongInput state.strong.copying (state.strong.originalFields owner)
      let next := writeFields { state.strong with copying := result.1 } (.source owner) result.2
      let prepared := { state with strong := next }
      let visited := rememberOwner input state owner
      have tailProgress : Progress (Generational.remember input.strongInput next rest).copying final := by
        simpa [Generational.remember, next, result] using progress
      have nextProgress := Progress.trans _ _ _ (Generational.remember_progress input.strongInput next rest) tailProgress
      have registrationNodup : (prepared.registered ++ input.heap.data.units.ownerUnits owner).Nodup := by
        apply List.Nodup.sublist (List.prefix_append _ (rest.flatMap input.heap.data.units.ownerUnits)).sublist
        simpa [rememberOwners_registered, List.append_assoc, prepared] using nodup
      have preparedInvariant : UnitInvariant input final prepared :=
        ⟨invariant.outside, invariant.compatible, invariant.settled⟩
      have visitedInvariant : UnitInvariant input final visited :=
        earlyVisits_preserves input final prepared _ preparedInvariant nextProgress registrationNodup
      apply ih visited visitedInvariant
      · simpa [visited, rememberOwner] using tailProgress
      · simpa [rememberOwners, visited, List.append_assoc] using nodup

/-- Roots and each remembered owner's strong/weak scan establish the invariant. -/
theorem beforeFinish_units (input : Input n k) :
    UnitInvariant input (Generational.evacuate input.strongInput).1.copying
      (drainWeak input n (start input)).1 := by
  let final := (Generational.evacuate input.strongInput).1.copying
  let initial : Copying.Weak.State n k :=
    ⟨⟨.empty, rawFields input.strongInput.copyInput, []⟩,
      fun id => (input.heap.data.units.templates id).initial, [], [], []⟩
  have h : UnitInvariant input final initial :=
    ⟨by simp [initial], by simp [initial], by simp [initial]⟩
  have progress : Progress initial.strong.copying final :=
    ⟨by simp [initial, CopyState.empty], by simp [initial, CopyState.empty]⟩
  have finalNodup : (drainWeak input n (start input)).1.registered.Nodup := by
    simpa [collectWeak, finishDeferred] using collection_registered_nodup input
  have startNodup := List.Nodup.sublist (drainWeak_prefix input n (start input)).sublist finalNodup
  have seeded := earlyVisits_preserves input final initial input.heap.data.units.external h progress
    (by simpa [initial] using input.heap.valid.storage.external_nodup)
  let rooted := { earlyVisits input initial input.heap.data.units.external with
    strong := Generational.seedRoots input.strongInput }
  have rootInvariant : UnitInvariant input final rooted := ⟨seeded.outside, seeded.compatible, seeded.settled⟩
  have started : UnitInvariant input final (start input) := by
    apply rememberOwners_units input final rooted input.strongInput.remembered rootInvariant
    · exact Generational.drain_progress input.strongInput n (Generational.start input.strongInput)
    · exact startNodup
  apply drainWeak_units input final n (start input) started
  · rw [start_strong]; rfl
  · exact finalNodup

end CloverGC.Generational.Weak
