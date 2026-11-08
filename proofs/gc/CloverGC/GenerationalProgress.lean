import CloverGC.Generational
import CloverGC.CopyProgress

namespace CloverGC.Generational

open Copying

theorem discover_progress (input : CollectionInput n) (state : CopyState n) (reference : Reference n) :
    Progress state (discover input state reference).1 := by
  cases reference with
  | copied _ => exact Progress.refl state
  | source object =>
      cases inside : input.selected object with
      | false => simpa [discover, inside] using Progress.refl state
      | true => simpa [discover, inside] using discovery_progress input.copyInput state (.source object)

theorem repair_progress (input : CollectionInput n) (state : CopyState n) (references : List (Reference n)) :
    Progress state (repair input state references).1 := by
  induction references generalizing state with
  | nil => exact Progress.refl state
  | cons reference rest ih =>
      exact Progress.trans _ _ _ (discover_progress input state reference) (ih (discover input state reference).1)

theorem remember_progress (input : CollectionInput n) (state : EvacuationState n) (owners : List (Fin n)) :
    Progress state.copying (remember input state owners).copying := by
  induction owners generalizing state with
  | nil => exact Progress.refl state.copying
  | cons owner rest ih =>
      let result := repair input state.copying (state.originalFields owner)
      let next := writeFields { state with copying := result.1 } (.source owner) result.2
      have first : Progress state.copying next.copying := repair_progress input state.copying (state.originalFields owner)
      simpa [remember, result, next] using Progress.trans _ _ _ first (ih next)

theorem scan_progress (input : CollectionInput n) (state next : EvacuationState n) (reference : Reference n)
    (step : scan input state = some (reference, next)) : Progress state.copying next.copying := by
  cases queue : state.copying.pending with
  | nil => simp [scan, queue] at step
  | cons head rest =>
      let popped : CopyState n := { state.copying with pending := rest }
      let repaired := repair input popped (liveFields state head)
      have eq : (head, writeFields { state with copying := repaired.1 } head repaired.2) =
          (reference, next) := by simpa [scan, queue] using step
      obtain ⟨rfl, rfl⟩ := Prod.mk.inj eq
      have progress := repair_progress input popped (liveFields state head)
      constructor
      · simpa using progress.forwarding
      · simpa using progress.marked

theorem drain_progress (input : CollectionInput n) (fuel : Nat) (state : EvacuationState n) :
    Progress state.copying (drain input fuel state).1.copying := by
  induction fuel generalizing state with
  | zero => exact Progress.refl state.copying
  | succ fuel ih =>
      cases step : scan input state with
      | none => simpa [drain, step] using Progress.refl state.copying
      | some pair =>
          simpa [drain, step] using
            Progress.trans _ _ _ (scan_progress input state pair.2 pair.1 step) (ih pair.2)

end CloverGC.Generational
