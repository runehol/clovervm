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


end CloverGC.Generational.Weak
