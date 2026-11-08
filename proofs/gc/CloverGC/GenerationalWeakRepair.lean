import CloverGC.GenerationalWeakProofs

namespace CloverGC.Generational.Weak

open Copying Copying.Weak

theorem early_field_dead_false (input : CollectionInput n) (state : CopyState n) (field : Option (Reference n)) :
    (repairField (resolve input state .early) field).dead = false := by
  cases field with
  | none => rfl
  | some reference =>
      have ne := resolve_early_not_dead input state reference
      cases result : resolve input state .early reference <;> simp_all [repairField]

theorem late_field_pending_false (input : CollectionInput n) (state : CopyState n) (field : Option (Reference n)) :
    (repairField (resolve input state .late) field).pending = false := by
  cases field with
  | none => rfl
  | some reference =>
      have ne := resolve_late_not_pending input state reference
      cases result : resolve input state .late reference <;> simp_all [repairField]

/-- An early rewrite cannot change the result of eventual late resolution. -/
theorem early_field_compatible (input : CollectionInput n) (before after : CopyState n)
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

theorem early_field_settled (input : CollectionInput n) (before after : CopyState n)
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

@[simp] theorem early_cell_dead_false (input : CollectionInput n) (state : CopyState n) (cell : WeakField (Reference n)) :
    (repairCell (resolve input state .early) cell).dead = false := by
  cases cell <;> simp [repairCell, early_field_dead_false]

@[simp] theorem late_cell_pending_false (input : CollectionInput n) (state : CopyState n) (cell : WeakField (Reference n)) :
    (repairCell (resolve input state .late) cell).pending = false := by
  cases cell <;> simp [repairCell, late_field_pending_false]

theorem early_field_present (input : CollectionInput n) (state : CopyState n) (reference : Reference n) :
    (repairField (resolve input state .early) (some reference)).target.isNone = false := by
  have ne := resolve_early_not_dead input state reference
  cases result : resolve input state .early reference <;> simp_all [repairField]

theorem early_cell_compatible (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (cell : WeakField (Reference n)) :
    repairCell (resolve input after .late)
        (repairCell (resolve input before .early) cell).cell =
      repairCell (resolve input after .late) cell := by
  cases cell with
  | immediate _ => rfl
  | reference target =>
      simpa [repairCell] using congrArg
        (fun result : FieldResult n => CellResult.mk (.reference result.target) result.pending result.dead)
        (early_field_compatible input before after progress target)

theorem early_cell_settled (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (cell : WeakField (Reference n))
    (settled : (repairCell (resolve input before .early) cell).pending = false) :
    repairCell (resolve input after .late) cell =
      ⟨(repairCell (resolve input before .early) cell).cell, false, false⟩ := by
  cases cell with
  | immediate _ => rfl
  | reference target =>
      simpa [repairCell] using congrArg
        (fun result : FieldResult n => CellResult.mk (.reference result.target) result.pending result.dead)
        (early_field_settled input before after progress target settled)

theorem repair_late_complete (input : CollectionInput n) (copying : CopyState n) (unit : Unit n) :
    (repairUnit (resolve input copying .late) unit).2 = .complete := by
  cases unit with
  | field cell => simp [repairUnit]
  | entry record =>
      simp only [repairUnit]
      split
      · rfl
      · simp only [finishEntry, late_field_pending_false, late_cell_pending_false,
          Bool.false_or, Bool.false_eq_true, ↓reduceIte]
        split <;> rfl

theorem early_unit_compatible (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n) :
    repairUnit (resolve input after .late) (repairUnit (resolve input before .early) unit).1 =
      repairUnit (resolve input after .late) unit := by
  cases unit with
  | field cell => simp [repairUnit, early_cell_compatible input before after progress]
  | entry record =>
      cases record with
      | mk first second payload =>
          cases first <;> cases second <;>
            simp [repairUnit, clear, finishEntry, early_field_dead_false,
              early_field_present, early_field_compatible input before after progress,
              early_cell_compatible input before after progress]

theorem early_complete_correct (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n)
    (complete : (repairUnit (resolve input before .early) unit).2 = .complete) :
    (repairUnit (resolve input before .early) unit).1 =
      (repairUnit (resolve input after .late) unit).1 := by
  cases unit with
  | field cell =>
      have settled : (repairCell (resolve input before .early) cell).pending = false := by
        cases pending : (repairCell (resolve input before .early) cell).pending <;>
          simp_all [repairUnit]
      simp [repairUnit, early_cell_settled input before after progress cell settled]
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
                      (repairField (resolve input before .early) (some first)).pending = false ∧
                      (repairField (resolve input before .early) (some second)).pending = false ∧
                      (repairCell (resolve input before .early) payload).pending = false := by
                    simpa [repairUnit, finishEntry, early_field_dead_false, and_assoc] using complete
                  simp [repairUnit, finishEntry, early_field_dead_false,
                    early_field_settled input before after progress _ settled.1,
                    early_field_settled input before after progress _ settled.2.1,
                    early_cell_settled input before after progress _ settled.2.2]

end CloverGC.Generational.Weak
