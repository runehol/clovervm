import CloverGC.GenerationalWeak
import CloverGC.GenerationalCorrectness
import CloverGC.WeakResolutionProofs

namespace CloverGC.Generational.Weak

open Copying Copying.Weak

theorem resolve_unselected (input : CollectionInput n) (copying : CopyState n)
    (phase : Phase) (object : Fin n) (outside : input.selected object = false) :
    resolve input copying phase (.source object) = .resolved (.source object) := by
  simp [resolve, outside]

theorem resolve_old (input : CollectionInput n) (copying : CopyState n)
    (phase : Phase) (object : Fin n) (young : input.collection = .young)
    (old : input.heap.data.isOld object) :
    resolve input copying phase (.source object) = .resolved (.source object) := by
  apply resolve_unselected
  simpa [CollectionInput.selected, young] using old

theorem full_resolve (input : CollectionInput n) (full : input.collection = .full)
    (copying : CopyState n) (phase : Phase) (reference : Reference n) :
    resolve input copying phase reference = Copying.Weak.resolve input.copyInput copying phase reference := by
  cases reference <;> simp [resolve, CollectionInput.selected, full, Copying.Weak.resolve]

/-- Graph-live selected objects resolve to their canonical final strong reference
    in either collection mode, including young objects reached through remembered owners. -/
theorem resolve_final_live (input : CollectionInput n) (object : Fin n) (live : Live input object) :
    resolve input (Generational.evacuate input).1.copying .late (.source object) =
      .resolved (canonical (Generational.evacuate input).1.copying object) := by
  have inside := live_selected input object live
  cases movable : input.copyInput.movable object with
  | false =>
      have forward := nonmoving_unforwarded input.copyInput _ object
        (evacuate_invariant input).tracking.copying movable
      have marked := (evacuate_reachable input object).mpr live
      simp only [canonical, forward] at marked
      simp [resolve, inside, Copying.Weak.resolve, movable, marked, canonical, forward]
  | true =>
      obtain ⟨index, forward⟩ := (evacuate_forwarded_iff input object).mpr ⟨live, movable⟩
      simp [resolve, inside, Copying.Weak.resolve, movable, forward, canonical]

/-- A selected object outside the strong graph closure is dead. Unselected old
    objects remain covered by resolve_unselected rather than this dead-target rule. -/
theorem resolve_final_dead (input : CollectionInput n) (object : Fin n)
    (inside : input.selected object = true) (dead : ¬ Live input object) :
    resolve input (Generational.evacuate input).1.copying .late (.source object) = .dead := by
  have forward : (Generational.evacuate input).1.copying.forwarding object = none := by
    cases h : (Generational.evacuate input).1.copying.forwarding object with
    | none => rfl
    | some index => exact False.elim (dead ((evacuate_forwarded_iff input object).mp ⟨index, h⟩).1)
  have unmarked : Reference.source object ∉ (Generational.evacuate input).1.copying.marked := by
    intro marked
    apply dead
    apply (evacuate_reachable input object).mp
    simpa [canonical, forward] using marked
  cases movable : input.copyInput.movable object <;>
    simp [resolve, inside, Copying.Weak.resolve, movable, forward, unmarked, unresolved]

theorem resolve_early_not_dead (input : CollectionInput n) (copying : CopyState n)
    (reference : Reference n) : resolve input copying .early reference ≠ .dead := by
  cases reference with
  | copied index => simp [resolve]
  | source object =>
      cases inside : input.selected object with
      | false => simp [resolve, inside]
      | true => simpa [resolve, inside] using Copying.Weak.resolve_early_not_dead input.copyInput copying (.source object)

theorem resolve_late_not_pending (input : CollectionInput n) (copying : CopyState n)
    (reference : Reference n) : resolve input copying .late reference ≠ .pending := by
  cases reference with
  | copied index => simp [resolve]
  | source object =>
      cases inside : input.selected object with
      | false => simp [resolve, inside]
      | true => simpa [resolve, inside] using Copying.Weak.resolve_late_not_pending input.copyInput copying (.source object)

/-- Early decisions remain valid as strong discovery adds marks and forwarding;
    resolved targets themselves are fixed points of late resolution. -/
theorem resolve_stable (input : CollectionInput n) (before after : CopyState n)
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
      cases inside : input.selected object with
      | false =>
          have same : Reference.source object = target := by simpa [resolve, inside] using resolved
          subst target
          simp [resolve, inside]
      | true =>
          have old := Copying.Weak.resolve_stable input.copyInput before after progress (.source object) target
            (by simpa [resolve, inside] using resolved)
          refine ⟨by simpa [resolve, inside] using old.1, ?_⟩
          cases target <;> simp [resolve, old.2]

/-- A successful source resolution leaves an unselected source, a nonmoving
    source, or an allocated copy; selected movable sources cannot escape. -/
theorem resolved_source_safe (input : CollectionInput n) (copying : CopyState n)
    (object : Fin n) (target : Reference n) (invariant : CopyInvariant input.copyInput copying)
    (resolved : resolve input copying .late (.source object) = .resolved target) :
    Safe input copying target := by
  cases inside : input.selected object with
  | false =>
      have same : Reference.source object = target := by simpa [resolve, inside] using resolved
      subst target
      exact Or.inl inside
  | true =>
      have canonical := Copying.Weak.resolved_source_canonical input.copyInput copying object target invariant
        (by simpa [resolve, inside] using resolved)
      cases target with
      | source other => exact Or.inr canonical
      | copied index => exact canonical

theorem resolved_safe (input : CollectionInput n) (copying : CopyState n)
    (reference target : Reference n) (invariant : CopyInvariant input.copyInput copying)
    (valid : ValidReference copying reference)
    (resolved : resolve input copying .late reference = .resolved target) :
    Safe input copying target := by
  cases reference with
  | source object => exact resolved_source_safe input copying object target invariant resolved
  | copied index =>
      have same : Reference.copied index = target := by simpa [resolve] using resolved
      subst target
      exact valid

end CloverGC.Generational.Weak
