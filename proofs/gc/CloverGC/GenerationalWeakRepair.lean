import CloverGC.GenerationalWeakProofs

namespace CloverGC.Generational.Weak

open Copying Copying.Weak

theorem early_field_dead_false (input : CollectionInput n) (state : CopyState n) (field : Option (Reference n)) :
    (repairField (resolve input state .early) field).dead = false :=
  Repair.early_field_dead_false (resolve input state .early) (resolve_early_not_dead input state) field

theorem late_field_pending_false (input : CollectionInput n) (state : CopyState n) (field : Option (Reference n)) :
    (repairField (resolve input state .late) field).pending = false :=
  Repair.late_field_pending_false (resolve input state .late) (resolve_late_not_pending input state) field

/-- An early rewrite cannot change the result of eventual late resolution. -/
theorem early_field_compatible (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (field : Option (Reference n)) :
    repairField (resolve input after .late)
        (repairField (resolve input before .early) field).target =
      repairField (resolve input after .late) field :=
  Repair.early_field_compatible (resolve input before .early) (resolve input after .late)
    (resolve_early_not_dead input before) (resolve_stable input before after progress) field

theorem early_field_settled (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (field : Option (Reference n))
    (settled : (repairField (resolve input before .early) field).pending = false) :
    repairField (resolve input after .late) field =
      ⟨(repairField (resolve input before .early) field).target, false, false⟩ :=
  Repair.early_field_settled (resolve input before .early) (resolve input after .late)
    (resolve_early_not_dead input before) (resolve_stable input before after progress) field settled

@[simp] theorem early_cell_dead_false (input : CollectionInput n) (state : CopyState n) (cell : WeakField (Reference n)) :
    (repairCell (resolve input state .early) cell).dead = false :=
  Repair.early_cell_dead_false (resolve input state .early) (resolve_early_not_dead input state) cell

@[simp] theorem late_cell_pending_false (input : CollectionInput n) (state : CopyState n) (cell : WeakField (Reference n)) :
    (repairCell (resolve input state .late) cell).pending = false :=
  Repair.late_cell_pending_false (resolve input state .late) (resolve_late_not_pending input state) cell

theorem early_field_present (input : CollectionInput n) (state : CopyState n) (reference : Reference n) :
    (repairField (resolve input state .early) (some reference)).target.isNone = false :=
  Repair.early_field_present (resolve input state .early) (resolve_early_not_dead input state) reference

theorem early_cell_compatible (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (cell : WeakField (Reference n)) :
    repairCell (resolve input after .late)
        (repairCell (resolve input before .early) cell).cell =
      repairCell (resolve input after .late) cell :=
  Repair.early_cell_compatible (resolve input before .early) (resolve input after .late)
    (resolve_early_not_dead input before) (resolve_stable input before after progress) cell

theorem early_cell_settled (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (cell : WeakField (Reference n))
    (settled : (repairCell (resolve input before .early) cell).pending = false) :
    repairCell (resolve input after .late) cell =
      ⟨(repairCell (resolve input before .early) cell).cell, false, false⟩ :=
  Repair.early_cell_settled (resolve input before .early) (resolve input after .late)
    (resolve_early_not_dead input before) (resolve_stable input before after progress) cell settled

theorem repair_late_complete (input : CollectionInput n) (copying : CopyState n) (unit : Unit n) :
    (repairUnit (resolve input copying .late) unit).2 = .complete :=
  Repair.repair_late_complete (resolve input copying .late) (resolve_late_not_pending input copying) unit

theorem early_unit_compatible (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n) :
    repairUnit (resolve input after .late) (repairUnit (resolve input before .early) unit).1 =
      repairUnit (resolve input after .late) unit :=
  Repair.early_unit_compatible (resolve input before .early) (resolve input after .late)
    (resolve_early_not_dead input before) (resolve_stable input before after progress) unit

theorem early_complete_correct (input : CollectionInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n)
    (complete : (repairUnit (resolve input before .early) unit).2 = .complete) :
    (repairUnit (resolve input before .early) unit).1 =
      (repairUnit (resolve input after .late) unit).1 :=
  Repair.early_complete_correct (resolve input before .early) (resolve input after .late)
    (resolve_early_not_dead input before) (resolve_stable input before after progress) unit complete

private theorem resolve_final_retained (input : CollectionInput n) (object : Fin n)
    (retained : Retained input object) :
    resolve input (Generational.evacuate input).1.copying .late (.source object) =
      .resolved (canonical (Generational.evacuate input).1.copying object) := by
  rcases retained with outside | live
  · have forward := (evacuate_unselected input object outside).1
    simpa only [canonical, forward] using resolve_unselected input _ .late object outside
  · exact resolve_final_live input object live

/-- Active units preserve all retained targets, including unselected old objects,
    and retain immediate/null payload positions. -/
theorem repair_retained_template (input : CollectionInput n) (template : Template n)
    (active : template.active = true)
    (all_retained : ∀ object, some object ∈ template.targets → Retained input object) :
    repairUnit (resolve input (Generational.evacuate input).1.copying .late) template.initial =
      (template.map (canonical (Generational.evacuate input).1.copying), .complete) := by
  let resolver := resolve input (Generational.evacuate input).1.copying .late
  have field (original : Option (Fin n)) (member : original ∈ template.targets) :
      repairField resolver (original.map Reference.source) =
        ⟨original.map (canonical (Generational.evacuate input).1.copying), false, false⟩ := by
    cases original with
    | none => rfl
    | some object => simp [resolver, repairField, resolve_final_retained input object (all_retained object member)]
  dsimp [resolver] at field
  apply Prod.ext
  · cases template with
    | field cell =>
        cases cell with
        | immediate value => rfl
        | reference target =>
            have fact := field target (by simp [RepairUnit.targets, Cell.target])
            simp [Template.initial, RepairUnit.map, Cell.map, repairUnit, repairCell, fact]
    | entry record =>
        cases record with
        | mk first second payload =>
            cases first with
            | none => simp [RepairUnit.active] at active
            | some first =>
                cases second with
                | none => simp [RepairUnit.active] at active
                | some second =>
                    have f := field (some first) (by simp [RepairUnit.targets])
                    have g := field (some second) (by simp [RepairUnit.targets])
                    have p := field payload.target (by simp [RepairUnit.targets])
                    cases payload <;>
                      simp_all [Template.initial, RepairUnit.map, Cell.map, Cell.target,
                        repairUnit, repairCell, finishEntry]
  · exact repair_late_complete input _ _

/-- Any selected graph-dead target, including a referenced payload, clears the
    entire unit. Unselected old targets are preserved by the retained rule. -/
theorem repair_dead_template (input : CollectionInput n) (template : Template n)
    (object : Fin n) (member : some object ∈ template.targets)
    (inside : input.selected object = true) (dead : ¬ Live input object) :
    repairUnit (resolve input (Generational.evacuate input).1.copying .late) template.initial =
      (clear template.initial, .complete) := by
  apply Copying.Weak.repair_dead_dominates _ template.initial (.source object)
  · rw [Template.initial, targets_map]
    exact List.mem_map.mpr ⟨some object, member, rfl⟩
  · exact resolve_final_dead input object inside dead

end CloverGC.Generational.Weak
