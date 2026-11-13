import CloverGC.Differential.HeapFormatProofs
import Init.Data.Range.Lemmas

namespace CloverGC.Differential.Format

/-- Every canonical wire identity is accepted with its specified logical ordinal. -/
theorem objectNumber_complete (id : ObjectId) (path : String) (valid : CanonicalObjectId id) :
    objectNumber id path = .ok (logicalOrdinal id) := by
  obtain ⟨prefixEq, nonempty, decimal, noZero, parsed, bounded⟩ := valid
  let digits := String.ofList (id.value.toList.drop 4)
  have digitsValid : (!digits.isEmpty && digits.toList.all (fun c => '0' ≤ c && c ≤ '9')) = true := by
    simp only [digits, nonempty, Bool.not_false, Bool.true_and, decimal]
  have leading : ¬(digits.length > 1 && digits.startsWith "0") = true := by
    simpa only [digits, Bool.and_eq_true, decide_eq_true_eq] using noZero
  simp only [digits] at digitsValid leading
  unfold objectNumber
  simp only [prefixEq, digitsValid, leading, parsed, Bool.false_eq_true, ↓reduceIte,
    bind, Except.bind, pure, Except.pure]
  split
  · rename_i overflow
    exfalso
    exact (Nat.not_lt.mpr bounded) overflow
  · rfl

private theorem forIn_checks_success (xs : List α) (check : α → Except String Unit)
    (valid : ∀ a ∈ xs, check a = .ok ()) :
    forIn xs PUnit.unit (fun a _ => do check a; pure (.yield PUnit.unit)) = .ok PUnit.unit := by
  induction xs with
  | nil => rfl
  | cons a rest ih =>
      simp only [List.forIn_cons, valid a (by simp), bind, Except.bind, pure, Except.pure]
      exact ih (fun a member => valid a (by simp [member]))

/-- Every array of reference-valid cells passes the executable indexed scan. -/
theorem checkCells_success (cells : Array Cell) (count : Nat) (path : Nat → String)
    (valid : ∀ cell ∈ cells.toList, CellReferencesValid count cell) :
    forIn cells.toList.zipIdx PUnit.unit (fun (pair : Cell × Nat) (_ : PUnit) => do
      checkCell pair.1 count (path pair.2)
      pure (.yield PUnit.unit)) = .ok PUnit.unit := by
  apply forIn_checks_success
  intro pair member
  exact (checkCell_correct _ _ _).mpr (valid pair.1 (List.fst_mem_of_mem_zipIdx member))

/-- Fresh original/external unit checking succeeds from independent content,
    freshness and owner-layout conditions, with the exact updated state. -/
theorem checkUnit_complete (path : String) (storage : UnitStorage) (count : Nat)
    (before : Std.HashSet Nat × Bool) (pair : WeakUnit × Nat)
    (notCopy : storage ≠ .destination) (absent : before.1.contains pair.1.unitId.value = false)
    (content : WeakContentsReferencesValid count pair.1.contents)
    (layout : pair.1.contents.isField = true → storage = .external ∨ before.2 = false) :
    checkUnit path storage count before pair =
      .ok (before.1.insert pair.1.unitId.value, before.2 || !pair.1.contents.isField) := by
  apply (checkUnit_correct _ _ _ _ _ _).mpr
  exact ⟨content, fun _ => absent, layout, by simp [notCopy]⟩

private theorem checkUnitList_success (pairs : List (WeakUnit × Nat))
    (before : Std.HashSet Nat × Bool) (ids : List Nat) (processed : List WeakUnit)
    (ledger : IdLedger ids before.1)
    (flag : before.2 = processed.any (fun unit => !unit.contents.isField))
    (notCopy : storage ≠ .destination)
    (references : ∀ pair ∈ pairs, WeakContentsReferencesValid count pair.1.contents)
    (layout : storage = .external ∨ FieldsBeforeEntries (processed ++ pairs.map Prod.fst))
    (unique : (ids ++ pairs.map (fun pair => pair.1.unitId.value)).Nodup) :
    ∃ after, pairs.foldlM (checkUnit path storage count) before = .ok after ∧
      IdLedger (ids ++ pairs.map (fun pair => pair.1.unitId.value)) after.1 := by
  induction pairs generalizing before ids processed with
  | nil => exact ⟨before, rfl, by simpa using ledger⟩
  | cons pair rest ih =>
      have absentList : pair.1.unitId.value ∉ ids := by
        intro inside
        exact (List.nodup_append.mp unique).2.2 _ inside _ (by simp) rfl
      have absent : before.1.contains pair.1.unitId.value = false := by
        simpa only [Bool.not_eq_true] using
          (show ¬before.1.contains pair.1.unitId.value = true from fun h => absentList ((ledger.members _).mp h))
      have canAddField : pair.1.contents.isField = true → storage = .external ∨ before.2 = false := by
        intro field
        rcases layout with external | ordered
        · exact Or.inl external
        · have noEntries : processed.any (fun unit => !unit.contents.isField) = false := by
            apply List.any_eq_false.mpr
            intro unit member
            have cross := (List.pairwise_append.mp ordered).2.2 unit member pair.1 (by simp)
            have priorField := cross field
            simp [priorField]
          exact Or.inr (flag.trans noEntries)
      let next := (before.1.insert pair.1.unitId.value, before.2 || !pair.1.contents.isField)
      have step : checkUnit path storage count before pair = .ok next :=
        checkUnit_complete _ _ _ _ _ notCopy absent (references pair (by simp)) canAddField
      have nextLedger : IdLedger (ids ++ [pair.1.unitId.value]) next.1 := ledger.insert _ absent
      have nextFlag : next.2 = (processed ++ [pair.1]).any (fun unit => !unit.contents.isField) := by
        simp [next, flag]
      have nextLayout : storage = .external ∨ FieldsBeforeEntries ((processed ++ [pair.1]) ++ rest.map Prod.fst) := by
        simpa [List.append_assoc] using layout
      have nextUnique : ((ids ++ [pair.1.unitId.value]) ++ rest.map (fun pair => pair.1.unitId.value)).Nodup := by
        simpa [List.append_assoc] using unique
      obtain ⟨after, done, finalLedger⟩ := ih next (ids ++ [pair.1.unitId.value]) (processed ++ [pair.1])
        nextLedger nextFlag (fun pair member => references pair (by simp [member])) nextLayout nextUnique
      refine ⟨after, ?_, ?_⟩
      · simpa only [List.foldlM_cons, step, bind, Except.bind] using done
      · simpa [List.append_assoc] using finalLedger

/-- The complete fresh-unit scan accepts every reference-valid, uniquely named
    unit array whose owner layout is valid. External units have no layout restriction. -/
theorem checkUnits_success (values : Array WeakUnit) (path : String) (storage : UnitStorage)
    (count : Nat) (seen : Std.HashSet Nat) (ids : List Nat) (ledger : IdLedger ids seen)
    (notCopy : storage ≠ .destination)
    (references : ∀ unit ∈ values.toList, WeakContentsReferencesValid count unit.contents)
    (layout : storage = .external ∨ FieldsBeforeEntries values.toList)
    (unique : (ids ++ values.toList.map (fun unit => unit.unitId.value)).Nodup) :
    ∃ result, checkUnits values path storage count seen = .ok result ∧
      IdLedger (ids ++ values.toList.map (fun unit => unit.unitId.value)) result := by
  have keys : values.toList.zipIdx.map (fun pair => pair.1.unitId.value) =
      values.toList.map (fun unit => unit.unitId.value) := by
    simpa only [List.map_map, Function.comp_def] using
      congrArg (List.map (fun unit => unit.unitId.value)) (List.zipIdx_map_fst 0 values.toList)
  obtain ⟨after, done, finalLedger⟩ := checkUnitList_success (path := path) values.toList.zipIdx (seen, false) ids [] ledger rfl notCopy
    (fun pair member => references pair.1 (List.fst_mem_of_mem_zipIdx member))
    (by simpa only [List.nil_append, List.zipIdx_map_fst] using layout)
    (by simpa only [keys] using unique)
  refine ⟨after.1, ?_, ?_⟩
  · simp [checkUnits, done] <;> rfl
  · simpa only [keys] using finalLedger

/-- A valid original allocation passes its complete record check, preserving
    copy-table state and extending exactly its origin slot and unit ledger. -/
theorem validateOriginalAllocation_success (count originalCount : Nat) (before : AllocationTables)
    (pair : Allocation × Nat) (ids : List Nat)
    (original : isDestination pair.1.state = false)
    (physical : pair.1.allocationId.value = pair.2)
    (within : pair.1.allocationId.value < originalCount)
    (canonical : CanonicalObjectId pair.1.objectId)
    (slot : logicalOrdinal pair.1.objectId < before.origins.size)
    (emptySlot : before.origins[logicalOrdinal pair.1.objectId] = none)
    (references : AllocationCellReferencesValid count pair.1)
    (layout : FieldsBeforeEntries pair.1.weak.toList)
    (ledger : IdLedger ids before.seen)
    (unique : (ids ++ pair.1.weak.toList.map (fun unit => unit.unitId.value)).Nodup) :
    ∃ after, validateAllocation count originalCount before pair = .ok after ∧
      after.origins = before.origins.set (logicalOrdinal pair.1.objectId) (some pair.1) ∧
      after.copies = before.copies ∧
      IdLedger (ids ++ pair.1.weak.toList.map (fun unit => unit.unitId.value)) after.seen := by
  have parsed := objectNumber_complete pair.1.objectId (indexPath "$.objects" pair.2 ++ ".object_id") canonical
  have strong := checkCells_success pair.1.strong count
    (fun index => indexPath (indexPath "$.objects" pair.2 ++ ".strong") index) references.1
  simp only [bind, Except.bind, pure, Except.pure] at strong
  obtain ⟨seen, weak, updatedLedger⟩ := checkUnits_success pair.1.weak
    (indexPath "$.objects" pair.2 ++ ".weak") .original count before.seen ids ledger
    (by decide) references.2 (Or.inr layout) unique
  let after : AllocationTables :=
    ⟨before.origins.set (logicalOrdinal pair.1.objectId) (some pair.1), before.copies, seen⟩
  refine ⟨after, ?_, rfl, rfl, updatedLedger⟩
  have withinIndex : pair.2 < originalCount := by simpa only [physical] using within
  simp [validateAllocation, original, physical, withinIndex, parsed, slot, emptySlot,
    strong, weak, after, bind, Except.bind, pure, Except.pure]


/-- Every valid old remembered owner is accepted exactly once. -/
theorem checkRememberedOwner_complete (heap : HeapSnapshot) (before : Std.HashSet Nat)
    (id : AllocationId)
    (owner : ∃ a, heap.objects[id.value]? = some a ∧ OldOriginal a.state)
    (absent : before.contains id.value = false) :
    checkRememberedOwner heap before id = .ok (before.insert id.value) := by
  obtain ⟨a, lookup, old⟩ := owner
  cases state : a.state <;> simp only [state, OldOriginal] at old
  all_goals simp [checkRememberedOwner, lookup, state, absent, pure, Except.pure]

private theorem checkRememberedList_success (ids : List AllocationId) (before : Std.HashSet Nat)
    (processed : List Nat) (ledger : IdLedger processed before)
    (owners : ∀ id ∈ ids, ∃ a, heap.objects[id.value]? = some a ∧ OldOriginal a.state)
    (unique : (processed ++ ids.map AllocationId.value).Nodup) :
    ∃ after, ids.foldlM (checkRememberedOwner heap) before = .ok after ∧
      IdLedger (processed ++ ids.map AllocationId.value) after := by
  induction ids generalizing before processed with
  | nil => exact ⟨before, rfl, by simpa using ledger⟩
  | cons id rest ih =>
      have absentList : id.value ∉ processed := by
        intro inside
        exact (List.nodup_append.mp unique).2.2 _ inside _ (by simp) rfl
      have absent : before.contains id.value = false := by
        simpa only [Bool.not_eq_true] using
          (show ¬before.contains id.value = true from fun h => absentList ((ledger.members _).mp h))
      have step := checkRememberedOwner_complete heap before id (owners id (by simp)) absent
      obtain ⟨after, done, finalLedger⟩ := ih (before.insert id.value) (processed ++ [id.value])
        (ledger.insert _ absent) (fun id member => owners id (by simp [member]))
        (by simpa [List.append_assoc] using unique)
      refine ⟨after, ?_, ?_⟩
      · simpa only [List.foldlM_cons, step, bind, Except.bind] using done
      · simpa [List.append_assoc] using finalLedger

/-- The remembered scan accepts precisely named unique old owners and records
    their physical identities; no reachability or reference-strength premise is needed. -/
theorem checkRememberedOwners_success (heap : HeapSnapshot)
    (owners : RememberedOwnersValid heap) (distinct : heap.remembered.toList.Nodup) :
    ∃ seen, heap.remembered.toList.foldlM (checkRememberedOwner heap) {} = .ok seen ∧
      IdLedger (heap.remembered.toList.map AllocationId.value) seen := by
  have injective : Function.Injective AllocationId.value := by
    intro a b equal
    cases a; cases b; simp_all
  have unique : (heap.remembered.toList.map AllocationId.value).Nodup := distinct.map AllocationId.value (fun _ _ different equal => different (injective equal))
  simpa using checkRememberedList_success heap.remembered.toList {} [] IdLedger.empty owners unique

theorem forIn_yields_success (xs : List α)
    (body : α → PUnit → Except String (ForInStep PUnit))
    (valid : ∀ a ∈ xs, ∀ state, body a state = .ok (.yield state)) :
    forIn xs PUnit.unit body = .ok PUnit.unit := by
  induction xs with
  | nil => rfl
  | cons a rest ih =>
      simp only [List.forIn_cons, valid a (by simp), bind, Except.bind]
      exact ih (fun a member => valid a (by simp [member]))

/-- Strong-edge coverage checks accept owners using the same remembered set. -/
theorem checkStrongOwner_complete (heap : HeapSnapshot) (seen : Std.HashSet Nat) (owner : Allocation)
    (covered : owner.state.isYoung = false → ∀ id, .reference (some id) ∈ owner.strong.toList →
      ∀ target, heap.objects[id.value]? = some target → target.state.isYoung = true →
        seen.contains owner.allocationId.value = true) :
    checkStrongOwner heap seen owner = .ok () := by
  unfold checkStrongOwner
  cases state : owner.state
  all_goals simp only [Bool.not_true, Bool.not_false, Bool.false_eq_true, ↓reduceIte,
    pure, Except.pure]
  all_goals first | rfl | skip
  all_goals rw [← Array.forIn_toList]
  all_goals
    apply Eq.trans (congrArg (fun result : Except String PUnit =>
      Except.bind result (fun _ => Except.ok ())) (a₂ := .ok PUnit.unit) ?_) rfl
    apply forIn_yields_success
    intro value member cursor
    cases value with
    | immediate _ => rfl
    | reference ref =>
        cases ref with
        | none => rfl
        | some id =>
            cases lookup : heap.objects[id.value]? with
            | none => simp [lookup]
            | some target =>
                cases targetState : target.state <;> simp [lookup, targetState]
                have remembered := covered (by simp [state, ObjectState.isYoung]) id member target lookup
                  (by simp [targetState, ObjectState.isYoung])
                intro missing
                exact False.elim (missing (by simpa using remembered))

/-- A covered heap passes the whole strong-edge scan with its remembered ledger. -/
theorem checkStrongOwners_success (heap : HeapSnapshot) (seen : Std.HashSet Nat)
    (ledger : IdLedger (heap.remembered.toList.map AllocationId.value) seen)
    (covered : StrongRememberedCovered heap) :
    forIn heap.objects PUnit.unit (fun owner _ => do
      checkStrongOwner heap seen owner
      pure (.yield PUnit.unit)) = .ok PUnit.unit := by
  rw [← Array.forIn_toList]
  apply forIn_checks_success
  intro owner member
  apply checkStrongOwner_complete
  intro old id inside target lookup young
  exact (ledger.members _).mpr (covered owner member old id inside target lookup young)

/-- A complete dense ID ledger passes the final unit-density check. -/
theorem checkDenseUnitIds_complete (seen : Std.HashSet Nat) (ids : List Nat)
    (ledger : IdLedger ids seen) (dense : ids.Perm (List.range ids.length)) :
    checkDenseUnitIds seen = .ok () := by
  unfold checkDenseUnitIds
  apply Eq.trans (congrArg (fun result : Except String PUnit =>
    Except.bind result (fun _ => Except.ok ())) (a₂ := .ok PUnit.unit) ?_) rfl
  apply forIn_yields_success
  intro id inside cursor
  have member : id ∈ ids := (ledger.members _).mp (by simpa using inside)
  have bounded : id < seen.size := by
    rw [ledger.size]
    exact List.mem_range.mp (dense.mem_iff.mp member)
  cases cursor
  simp [bounded, pure, Except.pure]

end CloverGC.Differential.Format
