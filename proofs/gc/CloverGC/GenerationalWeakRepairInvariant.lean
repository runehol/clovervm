import CloverGC.GenerationalWeakRepair
import CloverGC.GenerationalWeakScheduling

namespace CloverGC.Generational.Weak

open Copying

/-- Direct late repair of a template relative to the eventual strong result. -/
def desired (input : Input n k) (final : CopyState n) (id : Fin k) : Copying.Weak.Unit n :=
  (Copying.Weak.repairUnit (resolve input.strongInput final .late)
    (input.heap.data.units.templates id).initial).1

/-- Semantic unit values only. Queue order and uniqueness are established
    separately by the scheduling proofs. The eventual strong result is fixed. -/
structure UnitInvariant (input : Input n k) (final : CopyState n)
    (state : Copying.Weak.State n k) : Prop where
  outside : ∀ id, id ∉ state.registered → state.units id = (input.heap.data.units.templates id).initial
  compatible : ∀ id ∈ state.registered,
    Copying.Weak.repairUnit (resolve input.strongInput final .late) (state.units id) =
      Copying.Weak.repairUnit (resolve input.strongInput final .late) (input.heap.data.units.templates id).initial
  settled : ∀ id ∈ state.registered, id ∉ state.pending → state.units id = desired input final id

@[simp] theorem earlyVisit_registered (input : Input n k) (state : Copying.Weak.State n k) (id : Fin k) :
    (earlyVisit input state id).registered = state.registered ++ [id] := rfl

@[simp] theorem earlyVisit_units (input : Input n k) (state : Copying.Weak.State n k) (id other : Fin k) :
    (earlyVisit input state id).units other =
      if other = id then (Copying.Weak.repairUnit (resolve input.strongInput state.strong.copying .early)
        (state.units id)).1 else state.units other := rfl

@[simp] theorem earlyVisit_pending (input : Input n k) (state : Copying.Weak.State n k) (id : Fin k) :
    (earlyVisit input state id).pending =
      if (Copying.Weak.repairUnit (resolve input.strongInput state.strong.copying .early) (state.units id)).2 = .pending
      then state.pending ++ [id] else state.pending := rfl

theorem earlyVisit_preserves (input : Input n k) (final : CopyState n)
    (state : Copying.Weak.State n k) (id : Fin k) (invariant : UnitInvariant input final state)
    (progress : Progress state.strong.copying final) (fresh : id ∉ state.registered) :
    UnitInvariant input final (earlyVisit input state id) := by
  have originalUnit := invariant.outside id fresh
  refine ⟨?_, ?_, ?_⟩
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
      simpa [originalUnit] using early_unit_compatible input.strongInput state.strong.copying final
        progress (input.heap.data.units.templates id).initial
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
      have complete : (Copying.Weak.repairUnit (resolve input.strongInput state.strong.copying .early)
          (state.units id)).2 = .complete := by
        cases status : (Copying.Weak.repairUnit (resolve input.strongInput state.strong.copying .early)
            (state.units id)).2 with
        | complete => rfl
        | pending => simp [status] at not_pending_new
      simpa [originalUnit, desired] using early_complete_correct input.strongInput state.strong.copying final
        progress (input.heap.data.units.templates id).initial (by simpa [originalUnit] using complete)

theorem earlyVisits_preserves (input : Input n k) (final : CopyState n)
    (state : Copying.Weak.State n k) (ids : List (Fin k)) (invariant : UnitInvariant input final state)
    (progress : Progress state.strong.copying final) (nodup : (state.registered ++ ids).Nodup) :
    UnitInvariant input final (earlyVisits input state ids) := by
  induction ids generalizing state with
  | nil => exact invariant
  | cons id rest ih =>
      have fresh : id ∉ state.registered := by
        have disjoint := (List.nodup_append.mp nodup).2.2
        intro member
        exact disjoint id member id (by simp) rfl
      apply ih (earlyVisit input state id) (earlyVisit_preserves input final state id invariant progress fresh)
      · exact progress
      · simpa [List.append_assoc] using nodup

end CloverGC.Generational.Weak
