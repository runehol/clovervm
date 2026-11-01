import CloverGC.EvacuationFields

namespace CloverGC.Copying

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

end CloverGC.Copying
