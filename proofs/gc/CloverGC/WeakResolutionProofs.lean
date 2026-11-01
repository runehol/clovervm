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

theorem repair_late_complete (input : CopyInput n) (copying : CopyState n) (unit : Unit n) :
    (repairUnit (resolve input copying .late) unit).2 = .complete := by
  simp [repairUnit, List.any_map, late_field_pending_false]
  split <;> rfl

theorem repair_early_payload (input : CopyInput n) (copying : CopyState n) (unit : Unit n) :
    (repairUnit (resolve input copying .early) unit).1.payload = unit.payload := by
  simp [repairUnit, List.any_map, early_field_dead_false]

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

theorem repair_early_form (input : CopyInput n) (state : CopyState n) (unit : Unit n) :
    repairUnit (resolve input state .early) unit =
      (⟨unit.targets.map (fun field => (repairField (resolve input state .early) field).target), unit.payload⟩,
       if (unit.targets.map (repairField (resolve input state .early))).any FieldResult.pending
       then .pending else .complete) := by
  simp [repairUnit, List.any_map, early_field_dead_false, List.map_map, Function.comp_def]

theorem early_unit_compatible (input : CopyInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n) :
    repairUnit (resolve input after .late) (repairUnit (resolve input before .early) unit).1 =
      repairUnit (resolve input after .late) unit := by
  rw [repair_early_form]
  simp only [repairUnit, List.map_map, Function.comp_def]
  have same : unit.targets.map (fun field => repairField (resolve input after .late)
      (repairField (resolve input before .early) field).target) =
      unit.targets.map (repairField (resolve input after .late)) :=
    List.map_congr_left (fun field _ => early_field_compatible input before after progress field)
  rw [same]
  split
  · simp [clear, List.map_map, Function.comp_def]
  · have targets := congrArg (List.map FieldResult.target) same
    simp only [List.map_map, Function.comp_def] at targets
    rw [targets]

theorem early_complete_correct (input : CopyInput n) (before after : CopyState n)
    (progress : Progress before after) (unit : Unit n)
    (complete : (repairUnit (resolve input before .early) unit).2 = .complete) :
    (repairUnit (resolve input before .early) unit).1 =
      (repairUnit (resolve input after .late) unit).1 := by
  have no_pending : ∀ field ∈ unit.targets,
      (repairField (resolve input before .early) field).pending = false := by
    rw [repair_early_form] at complete
    split at complete
    · contradiction
    · rename_i no
      simpa [List.any_map, List.any_eq_false] using no
  have facts (field) (member : field ∈ unit.targets) :=
    early_field_settled input before after progress field (no_pending field member)
  have dead : (unit.targets.map (repairField (resolve input after .late))).any FieldResult.dead = false := by
    simp only [List.any_eq_false, List.mem_map]
    rintro result ⟨field, member, rfl⟩
    simp [facts field member]
  rw [repair_early_form]
  simp only [repairUnit, dead, Bool.false_eq_true, ↓reduceIte]
  congr 1
  simp only [List.map_map, Function.comp_def]
  exact List.map_congr_left (fun field member =>
    (congrArg FieldResult.target (facts field member)).symm)

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
  simp only [repairUnit] at member
  split at member
  · simp [clear] at member
  · obtain ⟨result, inResults, eq⟩ := List.mem_map.mp member
    obtain ⟨field, inFields, rfl⟩ := List.mem_map.mp inResults
    exact ⟨field, inFields, eq⟩

theorem repair_template_safe (input : CopyInput n) (copying : CopyState n) (template : Template n)
    (h : CopyInvariant input copying) (reference : Reference n)
    (member : some reference ∈ (repairUnit (resolve input copying .late) template.initial).1.targets) :
    Canonical input copying reference := by
  obtain ⟨field, inFields, eq⟩ := repairUnit_target _ _ reference member
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

theorem repair_live_template (input : CopyInput n) (roots : List (Fin n)) (template : Template n)
    (all_live : ∀ object, some object ∈ template.targets → Reachable input.heap roots object) :
    (repairUnit (resolve input (evacuate input roots).1.copying .late) template.initial).1 =
      ⟨template.targets.map (Option.map (canonical (evacuate input roots).1.copying)), template.payload⟩ := by
  let resolver := resolve input (evacuate input roots).1.copying .late
  have field (original : Option (Fin n)) (member : original ∈ template.targets) :
      repairField resolver (original.map Reference.source) =
        ⟨original.map (canonical (evacuate input roots).1.copying), false, false⟩ := by
    cases original with
    | none => rfl
    | some object => simp [resolver, repairField, resolve_final_live input roots object (all_live object member)]
  have no_dead : (template.initial.targets.map (repairField resolver)).any FieldResult.dead = false := by
    simp only [List.any_eq_false, List.mem_map]
    rintro result ⟨target, inTargets, rfl⟩
    obtain ⟨original, inOriginal, rfl⟩ := List.mem_map.mp inTargets
    simp [field original inOriginal]
  change (repairUnit resolver template.initial).1 = _
  simp only [repairUnit, no_dead, Bool.false_eq_true, ↓reduceIte]
  congr 1
  simp only [Template.initial, List.map_map, Function.comp_def]
  exact List.map_congr_left (fun original member => congrArg FieldResult.target (field original member))

theorem repair_dead_dominates (resolver : Reference n → Resolution n) (unit : Unit n)
    (reference : Reference n) (member : some reference ∈ unit.targets)
    (dead : resolver reference = .dead) :
    repairUnit resolver unit = (clear unit, .complete) := by
  have has_dead : (unit.targets.map (repairField resolver)).any FieldResult.dead = true := by
    apply List.any_eq_true.mpr
    refine ⟨repairField resolver (some reference), List.mem_map.mpr ⟨some reference, member, rfl⟩, ?_⟩
    simp [repairField, dead]
  simp [repairUnit, has_dead]

theorem repair_dead_template (input : CopyInput n) (roots : List (Fin n)) (template : Template n)
    (object : Fin n) (member : some object ∈ template.targets)
    (dead : ¬ Reachable input.heap roots object) :
    repairUnit (resolve input (evacuate input roots).1.copying .late) template.initial =
      (clear template.initial, .complete) :=
  repair_dead_dominates _ template.initial (.source object)
    (List.mem_map.mpr ⟨some object, member, rfl⟩) (resolve_final_dead input roots object dead)

end CloverGC.Copying.Weak
