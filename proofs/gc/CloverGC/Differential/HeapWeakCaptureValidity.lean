import CloverGC.Differential.HeapPreparationProofs
import CloverGC.WeakRepairProofs
import CloverGC.GenerationalWeakSafety

namespace CloverGC.Differential

/-- Capture validity observes every stored reference, including inactive entries. -/
theorem UnitReferencesValid.of_targets (copying : Copying.CopyState n) (unit : Copying.Weak.Unit n)
    (safe : ∀ reference, some reference ∈ unit.targets → Copying.ValidReference copying reference) :
    UnitReferencesValid copying unit := by
  cases unit with
  | field cell =>
      cases cell with
      | immediate value => trivial
      | reference target =>
          cases target with
          | none => trivial
          | some reference => exact safe reference (by simp [Copying.Weak.RepairUnit.targets, Copying.Weak.Cell.target])
  | entry entry =>
      refine ⟨?_, ?_, ?_⟩
      · intro reference member
        exact safe reference (by simp [Copying.Weak.RepairUnit.targets, Option.mem_def] at member ⊢; exact Or.inl member.symm)
      · intro reference member
        exact safe reference (by simp [Copying.Weak.RepairUnit.targets, Option.mem_def] at member ⊢; exact Or.inr (Or.inl member.symm))
      · cases payload : entry.payload with
        | immediate value => trivial
        | reference target =>
            cases target with
            | none => trivial
            | some reference => exact safe reference (by simp [Copying.Weak.RepairUnit.targets, payload, Copying.Weak.Cell.target])

theorem ModelTemplate.initial_kind (model : ModelTemplate objects before template) :
    UnitKindMatches before template.initial := by
  cases before <;> cases template <;>
    simp_all [ModelTemplate, UnitKindMatches, Copying.Weak.Template.initial, Copying.Weak.RepairUnit.map]

theorem UnitKindMatches.repair {before : Format.WeakContents} {unit : Copying.Weak.Unit n} (kind : UnitKindMatches before unit)
    (resolver : Copying.Reference n → Copying.Weak.Resolution n) :
    UnitKindMatches before (Copying.Weak.repairUnit resolver unit).1 := by
  cases before <;> cases unit <;> simp only [UnitKindMatches] at kind
  all_goals try contradiction
  · trivial
  · simp only [Copying.Weak.repairUnit, Copying.Weak.finishEntry]
    repeat' first | trivial | split

theorem template_initial_valid (copying : Copying.CopyState n) (template : Copying.Weak.Template n) :
    UnitReferencesValid copying template.initial := by
  apply UnitReferencesValid.of_targets
  intro reference member
  rw [Copying.Weak.Template.initial, Copying.Weak.targets_map] at member
  obtain ⟨original, _, same⟩ := List.mem_map.mp member
  cases original with
  | none => simp at same
  | some object =>
      have equal : Copying.Reference.source object = reference := by simpa using same
      subst reference
      trivial

theorem full_unregistered_unchanged (input : Copying.CopyInput n) (weak : Copying.Weak.Input n k)
    (roots : List (Fin n)) (wf : Copying.Weak.WellFormed weak) (id : Fin k)
    (unregistered : id ∉ (Copying.Weak.collectWeak input weak roots).registered) :
    (Copying.Weak.collectWeak input weak roots).units id = (weak.templates id).initial := by
  let before := Copying.Weak.drainWeak input weak n (Copying.Weak.start input weak roots)
  have invariant := Copying.Weak.beforeFinish_units input weak roots wf
  have outside : id ∉ before.registered := by
    simpa [Copying.Weak.collectWeak, Copying.Weak.finishDeferred, before] using unregistered
  have notPending : id ∉ before.pending := fun member => outside (invariant.pending_registered id member)
  change (Copying.Weak.lateVisits input before before.pending).units id = _
  rw [Copying.Weak.lateVisits_unmentioned input before before.pending id notPending]
  exact invariant.outside id outside

/-- Full weak collection preserves layout kind and leaves only allocated stored references. -/
theorem weakCaptureReady_full (input : Copying.CopyInput n) (weak : Copying.Weak.Input n k)
    (roots : List (Fin n)) (wf : Copying.Weak.WellFormed weak) (id : Fin k)
    (kind : UnitKindMatches before (weak.templates id).initial) :
    UnitKindMatches before ((Copying.Weak.collectWeak input weak roots).units id) ∧
      UnitReferencesValid (Copying.Weak.collectWeak input weak roots).strong.copying
        ((Copying.Weak.collectWeak input weak roots).units id) := by
  by_cases registered : id ∈ (Copying.Weak.collectWeak input weak roots).registered
  · constructor
    · rw [Copying.Weak.collection_units_correct input weak roots wf id registered]
      exact UnitKindMatches.repair kind _
    · apply UnitReferencesValid.of_targets
      intro reference member
      have safe := Copying.Weak.collection_disposal_safe input weak roots wf id registered reference member
      cases reference with
      | source object => trivial
      | copied index => exact safe
  · rw [full_unregistered_unchanged input weak roots wf id registered]
    exact ⟨kind, template_initial_valid _ _⟩

/-- Generational weak collection has the same capture guarantees for every unit. -/
theorem weakCaptureReady_generational (input : Generational.Weak.Input n k) (id : Fin k)
    (kind : UnitKindMatches before (input.heap.data.units.templates id).initial) :
    UnitKindMatches before ((Generational.Weak.collectWeak input).1.units id) ∧
      UnitReferencesValid (Generational.Weak.collectWeak input).1.strong.copying
        ((Generational.Weak.collectWeak input).1.units id) := by
  by_cases registered : id ∈ (Generational.Weak.collectWeak input).1.registered
  · constructor
    · rw [Generational.Weak.collection_units_correct input id registered]
      exact UnitKindMatches.repair kind _
    · apply UnitReferencesValid.of_targets
      intro reference member
      have safe := Generational.Weak.collection_registered_safe input id registered reference member
      cases reference with
      | source object => trivial
      | copied index => exact safe
  · rw [Generational.Weak.collection_unregistered_unchanged input id registered]
    exact ⟨kind, template_initial_valid _ _⟩

end CloverGC.Differential
