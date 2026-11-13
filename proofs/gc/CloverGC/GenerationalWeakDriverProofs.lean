import CloverGC.GenerationalWeakDriver

namespace CloverGC.Generational.Weak

open Copying

@[simp] theorem earlyVisits_strong (input : Input n k) (state : Copying.Weak.State n k)
    (ids : List (Fin k)) : (earlyVisits input state ids).strong = state.strong := by
  induction ids generalizing state with
  | nil => rfl
  | cons id rest ih => exact ih (earlyVisit input state id)

@[simp] theorem lateVisits_strong (input : Input n k) (state : Copying.Weak.State n k)
    (ids : List (Fin k)) : (lateVisits input state ids).strong = state.strong := by
  induction ids generalizing state with
  | nil => rfl
  | cons id rest ih => exact ih (lateVisit input state id)

@[simp] theorem rememberOwners_strong (input : Input n k) (state : Copying.Weak.State n k)
    (owners : List (Fin n)) : (rememberOwners input state owners).strong =
      Generational.remember input.strongInput state.strong owners := by
  induction owners generalizing state with
  | nil => rfl
  | cons owner rest ih =>
      simpa [rememberOwners, rememberOwner, Generational.remember] using ih (rememberOwner input state owner)

@[simp] theorem start_strong (input : Input n k) :
    (start input).strong = Generational.start input.strongInput := by
  simp [start, Generational.start]

/-- Erasing weak storage preserves both the complete strong state and scan
    order, for every fuel and state, without any validity assumptions. -/
theorem drainWeak_strong (input : Input n k) (fuel : Nat) (state : Copying.Weak.State n k) :
    ((drainWeak input fuel state).1.strong, (drainWeak input fuel state).2) =
      Generational.drain input.strongInput fuel state.strong := by
  induction fuel generalizing state with
  | zero => rfl
  | succ fuel ih =>
      cases step : Generational.scan input.strongInput state.strong with
      | none => simp [drainWeak, Generational.drain, step]
      | some pair =>
          have tail := ih (earlyVisits input { state with strong := pair.2 }
            (Copying.Weak.ownerUnitsFor input.heap.data.units pair.2 pair.1))
          simp only [earlyVisits_strong] at tail
          simpa [drainWeak, Generational.drain, step, Prod.mk.injEq] using (Prod.ext_iff.mp tail)

theorem collectWeak_strong (input : Input n k) :
    ((collectWeak input).1.strong, (collectWeak input).2) =
      Generational.evacuate input.strongInput := by
  simpa [collectWeak, finishDeferred, Generational.evacuate] using
    drainWeak_strong input n (start input)

end CloverGC.Generational.Weak
