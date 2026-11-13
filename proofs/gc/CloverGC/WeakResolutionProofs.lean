import CloverGC.WeakRepair
import CloverGC.CopyProgress
import CloverGC.EvacuationProofs

namespace CloverGC.Copying.Weak

theorem resolve_early_not_dead (input : CopyInput n) (copying : CopyState n) (reference : Reference n) :
    resolve input copying .early reference ≠ .dead := by
  cases reference with
  | copied _ => simp [resolve]
  | source object =>
      cases movable : input.movable object <;>
        simp only [resolve, movable, Bool.false_eq_true, ↓reduceIte]
      · split <;> simp [unresolved]
      · cases copying.forwarding object <;> simp [unresolved]

theorem resolve_late_not_pending (input : CopyInput n) (copying : CopyState n) (reference : Reference n) :
    resolve input copying .late reference ≠ .pending := by
  cases reference with
  | copied _ => simp [resolve]
  | source object =>
      cases movable : input.movable object <;>
        simp only [resolve, movable, Bool.false_eq_true, ↓reduceIte]
      · split <;> simp [unresolved]
      · cases copying.forwarding object <;> simp [unresolved]

theorem early_field_dead_false (input : CopyInput n) (state : CopyState n) (field : Option (Reference n)) :
    (repairField (resolve input state .early) field).dead = false := by
  cases field with
  | none => rfl
  | some reference =>
      have ne := resolve_early_not_dead input state reference
      cases result : resolve input state .early reference <;> simp_all [repairField]

theorem late_field_pending_false (input : CopyInput n) (state : CopyState n) (field : Option (Reference n)) :
    (repairField (resolve input state .late) field).pending = false := by
  cases field with
  | none => rfl
  | some reference =>
      have ne := resolve_late_not_pending input state reference
      cases result : resolve input state .late reference <;> simp_all [repairField]

theorem resolve_stable (input : CopyInput n) (before after : CopyState n)
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
      cases movable : input.movable object with
      | false =>
          by_cases member : Reference.source object ∈ before.marked
          · have same : Reference.source object = target := by simpa [resolve, movable, member] using resolved
            subst target
            simp [resolve, movable, progress.marked _ member]
          · simp [resolve, movable, member, unresolved] at resolved
      | true =>
          cases forward : before.forwarding object with
          | none => simp [resolve, movable, forward, unresolved] at resolved
          | some index =>
              have same : Reference.copied index = target := by simpa [resolve, movable, forward] using resolved
              subst target
              simp [resolve, movable, progress.forwarding _ _ forward]

/-- An early rewrite cannot change the result of eventual late resolution. -/
theorem early_field_compatible (input : CopyInput n) (before after : CopyState n)
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

theorem early_field_settled (input : CopyInput n) (before after : CopyState n)
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

@[simp] theorem early_cell_dead_false (input : CopyInput n) (state : CopyState n) (cell : WeakField (Reference n)) :
    (repairCell (resolve input state .early) cell).dead = false := by
  cases cell <;> simp [repairCell, early_field_dead_false]

@[simp] theorem late_cell_pending_false (input : CopyInput n) (state : CopyState n) (cell : WeakField (Reference n)) :
    (repairCell (resolve input state .late) cell).pending = false := by
  cases cell <;> simp [repairCell, late_field_pending_false]

theorem early_field_present (input : CopyInput n) (state : CopyState n) (reference : Reference n) :
    (repairField (resolve input state .early) (some reference)).target.isNone = false := by
  have ne := resolve_early_not_dead input state reference
  cases result : resolve input state .early reference <;> simp_all [repairField]

theorem early_cell_compatible (input : CopyInput n) (before after : CopyState n)
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

theorem early_cell_settled (input : CopyInput n) (before after : CopyState n)
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

theorem repair_inactive (resolver : Reference n → Resolution n) (unit : Unit n)
    (inactive : unit.active = false) : repairUnit resolver unit = (clear unit, .complete) := by
  cases unit with
  | field _ => simp [RepairUnit.active] at inactive
  | entry record =>
      cases record with
      | mk first second payload =>
          cases first <;> cases second <;> simp_all [RepairUnit.active, repairUnit]

theorem repair_late_complete (input : CopyInput n) (copying : CopyState n) (unit : Unit n) :
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

theorem early_unit_compatible (input : CopyInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n) :
    repairUnit (resolve input after .late) (repairUnit (resolve input before .early) unit).1 =
      repairUnit (resolve input after .late) unit := by
  cases unit with
  | field cell => simp [repairUnit, early_cell_compatible input before after progress]
  | entry record =>
      cases record with
      | mk first second payload =>
          cases first with
          | none => simp [repairUnit, clear]
          | some first =>
              cases second with
              | none => simp [repairUnit, clear]
              | some second =>
                  simp [repairUnit, finishEntry, early_field_dead_false,
                    early_field_present, early_field_compatible input before after progress,
                    early_cell_compatible input before after progress]

theorem early_complete_correct (input : CopyInput n) (before after : CopyState n)
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
                    cases p : (repairField (resolve input before .early) (some first)).pending <;>
                      cases q : (repairField (resolve input before .early) (some second)).pending <;>
                      cases r : (repairCell (resolve input before .early) payload).pending <;>
                      simp_all [repairUnit, finishEntry, early_field_dead_false]
                  simp [repairUnit, finishEntry, early_field_dead_false,
                    early_field_settled input before after progress _ settled.1,
                    early_field_settled input before after progress _ settled.2.1,
                    early_cell_settled input before after progress _ settled.2.2]

/-- Active entries preserve immediate payloads during early repair; inactive
    entries are intentionally covered by repair_inactive instead. -/
theorem repair_early_immediate (input : CopyInput n) (copying : CopyState n)
    (first second : Reference n) (value : Int64) :
    (repairUnit (resolve input copying .early)
      (.entry ⟨some first, some second, .immediate value⟩)).1 =
      .entry ⟨(repairField (resolve input copying .early) (some first)).target,
        (repairField (resolve input copying .early) (some second)).target, .immediate value⟩ := by
  simp [repairUnit, finishEntry, repairCell, early_field_dead_false]

/-- Pointer payloads follow precisely the same weak relocation rules as guards;
    this includes null payload references, which never reach the resolver. -/
theorem repair_early_reference (input : CopyInput n) (copying : CopyState n)
    (first second : Reference n) (target : Option (Reference n)) :
    (repairUnit (resolve input copying .early)
      (.entry ⟨some first, some second, .reference target⟩)).1 =
      .entry ⟨(repairField (resolve input copying .early) (some first)).target,
        (repairField (resolve input copying .early) (some second)).target,
        .reference (repairField (resolve input copying .early) target).target⟩ := by
  simp [repairUnit, finishEntry, repairCell, early_field_dead_false]

@[simp] theorem targets_map (f : α → β) (unit : RepairUnit α) :
    (unit.map f).targets = unit.targets.map (Option.map f) := by
  cases unit with
  | field cell => cases cell <;> rfl
  | entry record =>
      cases record with
      | mk first second payload => cases payload <;> rfl

@[simp] theorem active_map (f : α → β) (unit : RepairUnit α) :
    (unit.map f).active = unit.active := by
  cases unit with
  | field _ => rfl
  | entry record =>
      cases record with
      | mk first second payload => cases first <;> cases second <;> rfl

@[simp] theorem repairCell_target (resolver : Reference n → Resolution n) (cell : WeakField (Reference n)) :
    (repairCell resolver cell).cell.target = (repairField resolver cell.target).target := by
  cases cell <;> rfl

theorem resolve_final_live (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (reachable : Reachable input.heap roots object) :
    resolve input (evacuate input roots).1.copying .late (.source object) =
      .resolved (canonical (evacuate input roots).1.copying object) := by
  cases movable : input.movable object with
  | false =>
      have fixed := evacuate_nonmoving input roots object movable
      have marked := (evacuate_reachable input roots object).mpr reachable
      rw [fixed] at marked
      simp [resolve, movable, marked, fixed]
  | true =>
      obtain ⟨index, forward⟩ := (evacuate_forwarded_iff input roots object).mpr ⟨reachable, movable⟩
      simp [resolve, movable, forward, canonical]

theorem resolve_final_dead (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (unreachable : ¬ Reachable input.heap roots object) :
    resolve input (evacuate input roots).1.copying .late (.source object) = .dead := by
  cases movable : input.movable object with
  | false =>
      have fixed := evacuate_nonmoving input roots object movable
      have unmarked : Reference.source object ∉ (evacuate input roots).1.copying.marked := by
        intro member
        exact unreachable ((evacuate_reachable input roots object).mp (fixed ▸ member))
      simp [resolve, movable, unmarked, unresolved]
  | true =>
      have unforwarded : (evacuate input roots).1.copying.forwarding object = none := by
        cases forward : (evacuate input roots).1.copying.forwarding object with
        | none => rfl
        | some index => exact False.elim (unreachable ((evacuate_forwarded_iff input roots object).mp ⟨index, forward⟩).1)
      simp [resolve, movable, unforwarded, unresolved]

theorem resolved_source_canonical (input : CopyInput n) (copying : CopyState n)
    (object : Fin n) (target : Reference n) (h : CopyInvariant input copying)
    (resolved : resolve input copying .late (.source object) = .resolved target) :
    Canonical input copying target := by
  cases movable : input.movable object with
  | false =>
      by_cases marked : Reference.source object ∈ copying.marked
      · have same : Reference.source object = target := by simpa [resolve, movable, marked] using resolved
        subst target
        exact movable
      · simp [resolve, movable, marked, unresolved] at resolved
  | true =>
      cases forward : copying.forwarding object with
      | none => simp [resolve, movable, forward, unresolved] at resolved
      | some index =>
          have same : Reference.copied index = target := by simpa [resolve, movable, forward] using resolved
          subst target
          obtain ⟨record, get, _⟩ := h.ledger.forwarding_valid object index forward
          exact ⟨record, get⟩

theorem repairUnit_target (resolver : Reference n → Resolution n) (unit : Unit n)
    (reference : Reference n) (member : some reference ∈ (repairUnit resolver unit).1.targets) :
    ∃ field ∈ unit.targets, (repairField resolver field).target = some reference := by
  cases unit with
  | field cell =>
      simp only [repairUnit, RepairUnit.targets, List.mem_singleton, repairCell_target] at member
      exact ⟨cell.target, by simp [RepairUnit.targets], member.symm⟩
  | entry record =>
      simp only [repairUnit] at member
      split at member
      · simp [clear, RepairUnit.targets, Cell.target] at member
      · simp only [finishEntry] at member
        split at member
        · simp [RepairUnit.targets, Cell.target] at member
        · simp only [RepairUnit.targets, List.mem_cons, List.not_mem_nil, or_false,
            repairCell_target] at member
          rcases member with first | second | payload
          · exact ⟨record.firstGuard, by simp [RepairUnit.targets], first.symm⟩
          · exact ⟨record.secondGuard, by simp [RepairUnit.targets], second.symm⟩
          · exact ⟨record.payload.target, by simp [RepairUnit.targets], payload.symm⟩

theorem repair_template_safe (input : CopyInput n) (copying : CopyState n) (template : Template n)
    (h : CopyInvariant input copying) (reference : Reference n)
    (member : some reference ∈ (repairUnit (resolve input copying .late) template.initial).1.targets) :
    Canonical input copying reference := by
  obtain ⟨field, inFields, eq⟩ := repairUnit_target _ _ reference member
  rw [Template.initial, targets_map] at inFields
  obtain ⟨original, _, rfl⟩ := List.mem_map.mp inFields
  cases original with
  | none => simp [repairField] at eq
  | some object =>
      cases resolved : resolve input copying .late (.source object) with
      | dead => simp [repairField, resolved] at eq
      | pending => exact False.elim (resolve_late_not_pending input copying (.source object) resolved)
      | resolved target =>
          have same : target = reference := by simpa [repairField, resolved] using eq
          subst reference
          exact resolved_source_canonical input copying object target h resolved

/-- The explicit active premise is required for mandatory guards. All referenced
    positions include the payload, unlike the old opaque-payload specification. -/
theorem repair_live_template (input : CopyInput n) (roots : List (Fin n)) (template : Template n)
    (active : template.active = true)
    (all_live : ∀ object, some object ∈ template.targets → Reachable input.heap roots object) :
    (repairUnit (resolve input (evacuate input roots).1.copying .late) template.initial).1 =
      template.map (canonical (evacuate input roots).1.copying) := by
  let resolver := resolve input (evacuate input roots).1.copying .late
  have field (original : Option (Fin n)) (member : original ∈ template.targets) :
      repairField resolver (original.map Reference.source) =
        ⟨original.map (canonical (evacuate input roots).1.copying), false, false⟩ := by
    cases original with
    | none => rfl
    | some object => simp [resolver, repairField, resolve_final_live input roots object (all_live object member)]
  dsimp [resolver] at field
  cases template with
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

/-- A dead reference anywhere, including the payload, clears the unit. An
    inactive entry already clears before any callback resolution takes place. -/
theorem repair_dead_dominates (resolver : Reference n → Resolution n) (unit : Unit n)
    (reference : Reference n) (member : some reference ∈ unit.targets)
    (dead : resolver reference = .dead) :
    repairUnit resolver unit = (clear unit, .complete) := by
  cases unit with
  | field cell =>
      cases cell with
      | immediate value => simp [RepairUnit.targets, Cell.target] at member
      | reference target =>
          have same : some reference = target := by simpa [RepairUnit.targets, Cell.target] using member
          subst target
          simp [repairUnit, repairCell, repairField, dead, clear]
  | entry record =>
      simp only [repairUnit]
      split
      · rfl
      · simp only [RepairUnit.targets, List.mem_cons, List.not_mem_nil, or_false] at member
        rcases member with first | second | payload
        · simp [finishEntry, ← first, repairField, dead, clear]
        · simp [finishEntry, ← second, repairField, dead, clear]
        · cases eq : record.payload with
          | immediate value => simp [eq, Cell.target] at payload
          | reference target =>
              have same : target = some reference := by simpa [eq, Cell.target] using payload.symm
              simp [finishEntry, same, repairCell, repairField, dead, clear]

theorem repair_dead_template (input : CopyInput n) (roots : List (Fin n)) (template : Template n)
    (object : Fin n) (member : some object ∈ template.targets)
    (dead : ¬ Reachable input.heap roots object) :
    repairUnit (resolve input (evacuate input roots).1.copying .late) template.initial =
      (clear template.initial, .complete) := by
  apply repair_dead_dominates _ template.initial (.source object)
  · rw [Template.initial, targets_map]
    exact List.mem_map.mpr ⟨some object, member, rfl⟩
  · exact resolve_final_dead input roots object dead

end CloverGC.Copying.Weak
