import CloverGC.WeakRepair

namespace CloverGC.Copying.Weak.Repair

/- Shared repair reasoning uses only phase and stability laws of the supplied
    resolvers; it does not depend on collection mode or strong-collector state. -/

variable (early late : Reference n → Resolution n)
variable (notDead : ∀ reference, early reference ≠ .dead)
variable (notPending : ∀ reference, late reference ≠ .pending)
variable (stable : ∀ reference target, early reference = .resolved target →
  late reference = .resolved target ∧ late target = .resolved target)

section Early
include notDead

theorem early_field_dead_false (field : Option (Reference n)) :
    (repairField early field).dead = false := by
  cases field with
  | none => rfl
  | some reference =>
      have ne := notDead reference
      cases result : early reference <;> simp_all [repairField]

theorem early_cell_dead_false (cell : WeakField (Reference n)) :
    (repairCell early cell).dead = false := by
  cases cell <;> simp [repairCell, early_field_dead_false early notDead]

theorem early_field_present (reference : Reference n) :
    (repairField early (some reference)).target.isNone = false := by
  have ne := notDead reference
  cases result : early reference <;> simp_all [repairField]

end Early

section Late
include notPending

theorem late_field_pending_false (field : Option (Reference n)) :
    (repairField late field).pending = false := by
  cases field with
  | none => rfl
  | some reference =>
      have ne := notPending reference
      cases result : late reference <;> simp_all [repairField]

theorem late_cell_pending_false (cell : WeakField (Reference n)) :
    (repairCell late cell).pending = false := by
  cases cell <;> simp [repairCell, late_field_pending_false late notPending]

theorem repair_late_complete (unit : Unit n) :
    (repairUnit late unit).2 = .complete := by
  cases unit with
  | field cell => simp [repairUnit, late_cell_pending_false late notPending]
  | entry record =>
      simp only [repairUnit]
      split
      · rfl
      · simp only [finishEntry, late_field_pending_false late notPending,
          late_cell_pending_false late notPending, Bool.false_or, Bool.false_eq_true, ↓reduceIte]
        split <;> rfl

end Late

section Compatible
include notDead stable

theorem early_field_compatible (field : Option (Reference n)) :
    repairField late (repairField early field).target = repairField late field := by
  cases field with
  | none => rfl
  | some reference =>
      cases resolved : early reference with
      | dead => exact False.elim (notDead reference resolved)
      | pending => simp [repairField, resolved]
      | resolved target =>
          obtain ⟨lateResult, fixed⟩ := stable reference target resolved
          simp [repairField, resolved, lateResult, fixed]

theorem early_field_settled (field : Option (Reference n))
    (settled : (repairField early field).pending = false) :
    repairField late field =
      ⟨(repairField early field).target, false, false⟩ := by
  cases field with
  | none => rfl
  | some reference =>
      cases resolved : early reference with
      | dead => exact False.elim (notDead reference resolved)
      | pending => simp [repairField, resolved] at settled
      | resolved target =>
          simp [repairField, resolved, (stable reference target resolved).1]

theorem early_cell_compatible (cell : WeakField (Reference n)) :
    repairCell late (repairCell early cell).cell = repairCell late cell := by
  cases cell with
  | immediate _ => rfl
  | reference target =>
      simpa [repairCell] using congrArg
        (fun result : FieldResult n => CellResult.mk (.reference result.target) result.pending result.dead)
        (early_field_compatible early late notDead stable target)

theorem early_cell_settled (cell : WeakField (Reference n))
    (settled : (repairCell early cell).pending = false) :
    repairCell late cell =
      ⟨(repairCell early cell).cell, false, false⟩ := by
  cases cell with
  | immediate _ => rfl
  | reference target =>
      simpa [repairCell] using congrArg
        (fun result : FieldResult n => CellResult.mk (.reference result.target) result.pending result.dead)
        (early_field_settled early late notDead stable target settled)

theorem early_unit_compatible (unit : Unit n) :
    repairUnit late (repairUnit early unit).1 =
      repairUnit late unit := by
  cases unit with
  | field cell => simp [repairUnit, early_cell_compatible early late notDead stable]
  | entry record =>
      cases record with
      | mk first second payload =>
          cases first <;> cases second <;>
            simp [repairUnit, clear, finishEntry, early_field_dead_false early notDead,
              early_cell_dead_false early notDead,
              early_field_present early notDead, early_field_compatible early late notDead stable,
              early_cell_compatible early late notDead stable]

theorem early_complete_correct (unit : Unit n)
    (complete : (repairUnit early unit).2 = .complete) :
    (repairUnit early unit).1 =
      (repairUnit late unit).1 := by
  cases unit with
  | field cell =>
      have settled : (repairCell early cell).pending = false := by
        cases pending : (repairCell early cell).pending <;>
          simp_all [repairUnit]
      simp [repairUnit, early_cell_settled early late notDead stable cell settled]
  | entry record =>
      cases record with
      | mk first second payload =>
          cases first with
          | none => simp [repairUnit]
          | some first =>
              cases second with
              | none => simp [repairUnit]
              | some second =>
                  have settled :
                      (repairField early (some first)).pending = false ∧
                      (repairField early (some second)).pending = false ∧
                      (repairCell early payload).pending = false := by
                    simpa [repairUnit, finishEntry, early_field_dead_false early notDead,
                      early_cell_dead_false early notDead, and_assoc] using complete
                  simp [repairUnit, finishEntry, early_field_dead_false early notDead,
                    early_cell_dead_false early notDead,
                    early_field_settled early late notDead stable _ settled.1,
                    early_field_settled early late notDead stable _ settled.2.1,
                    early_cell_settled early late notDead stable _ settled.2.2]

end Compatible

end CloverGC.Copying.Weak.Repair
