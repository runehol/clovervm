import CloverGC.Differential.HeapFormat
import Init.Data.Array.Sort.Lemmas
import Init.Data.SInt.Lemmas
import Std.Data.HashSet.Lemmas

namespace CloverGC.Differential.Format

private theorem orderedHeap_coverage (input : HeapSnapshot) :
    WeakRememberedCovered (orderedHeap input) ↔ WeakRememberedCovered input := by
  simp only [WeakRememberedCovered, orderedHeap, ← Array.mem_def, Array.mem_mergeSort]

private theorem validateAndOrder_value (input : HeapSnapshot)
    (checked : { heap : HeapSnapshot // WeakRememberedCovered heap })
    (accepted : validateAndOrder input = .ok checked) : checked.val = orderedHeap input := by
  unfold validateAndOrder at accepted
  cases structureResult : validateStructure (orderedHeap input) with
  | error message => simp [structureResult, bind, Except.bind] at accepted
  | ok value =>
      simp only [structureResult, bind, Except.bind] at accepted
      split at accepted
      · simp only [pure, Except.pure, Except.ok.injEq] at accepted
        cases accepted
        rfl
      · contradiction

private theorem validateAndOrder_structure (input : HeapSnapshot)
    (checked : { heap : HeapSnapshot // WeakRememberedCovered heap })
    (accepted : validateAndOrder input = .ok checked) : validateStructure checked.val = .ok () := by
  rw [validateAndOrder_value input checked accepted]
  unfold validateAndOrder at accepted
  cases structureCheck : validateStructure (orderedHeap input) with
  | error message => simp [structureCheck, bind, Except.bind] at accepted
  | ok value => cases value; rfl

/-- Successful public validation certifies coverage of the supplied snapshot. -/
theorem validateHeap_weak_covered (heap : HeapSnapshot)
    (accepted : validateHeap heap = .ok ()) : WeakRememberedCovered heap := by
  unfold validateHeap at accepted
  cases result : validateAndOrder heap with
  | error message => simp [result, bind, Except.bind] at accepted
  | ok checked =>
      have covered := checked.property
      rw [validateAndOrder_value heap checked result] at covered
      exact (orderedHeap_coverage heap).mp covered

private theorem prepareFreshHeap_checked (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) :
    ∃ checked, validateAndOrder input = .ok checked ∧ checked.val = heap := by
  unfold prepareFreshHeap at accepted
  cases result : validateAndOrder input with
  | error message => simp [result, bind, Except.bind] at accepted
  | ok checked =>
      cases fresh : checkFreshHeap checked.val with
      | error message => simp [result, fresh, bind, Except.bind] at accepted
      | ok value =>
          simp [result, fresh, bind, Except.bind, pure, Except.pure] at accepted
          exact ⟨checked, rfl, accepted⟩

/-- Successful fresh preparation certifies coverage of the returned snapshot. -/
theorem prepareFreshHeap_weak_covered (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) : WeakRememberedCovered heap := by
  obtain ⟨checked, _, same⟩ := prepareFreshHeap_checked input heap accepted
  rw [← same]
  exact checked.property

/-- Fresh preparation only orders allocations; none are removed or replaced. -/
theorem prepareFreshHeap_objects_perm (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) :
    heap.objects.toList.Perm input.objects.toList := by
  obtain ⟨checked, validated, same⟩ := prepareFreshHeap_checked input heap accepted
  rw [← same, validateAndOrder_value input checked validated]
  exact Array.perm_iff_toList_perm.mp Array.mergeSort_perm

/-- Null references are valid; non-null references name an existing physical allocation. -/
def ReferenceValid (count : Nat) (ref : Reference) : Prop :=
  ∀ id ∈ ref, id.value < count

/-- Fresh inputs contain only unmarked original allocations without forwarding. -/
def FreshState : ObjectState → Prop
  | .youngOriginal _ (.normal false) | .oldOriginal _ (.normal false)
  | .nonmovingOriginal false => True
  | _ => False

def FreshStates (heap : HeapSnapshot) : Prop :=
  ∀ allocation ∈ heap.objects.toList, FreshState allocation.state

/-- Reference checking accepts exactly null or in-range physical allocation IDs. -/
theorem checkReference_correct (ref : Reference) (count : Nat) (path : String) :
    checkReference ref count path = .ok () ↔ ReferenceValid count ref := by
  cases ref with
  | none => simp [checkReference, ReferenceValid, pure, Except.pure]
  | some id =>
      by_cases valid : id.value < count <;>
        simp [checkReference, ReferenceValid, valid, pure, Except.pure]
      intro impossible
      cases impossible

/-- The fresh-state check accepts exactly the independent state predicate. -/
theorem checkFreshHeap_correct (heap : HeapSnapshot) :
    checkFreshHeap heap = .ok () ↔ FreshStates heap := by
  unfold checkFreshHeap FreshStates
  rw [← Array.forIn_toList]
  generalize heap.objects.toList = objects
  induction objects with
  | nil => simp [List.forIn_nil, bind, Except.bind, pure, Except.pure]
  | cons a rest ih =>
      cases state : a.state with
      | youngOriginal policy header | oldOriginal policy header =>
          cases header with
          | normal marked => cases marked <;>
              simp_all [List.forIn_cons, FreshState, bind, Except.bind, pure, Except.pure] <;>
                (intro impossible; cases impossible)
          | forwarded target =>
              simp_all [List.forIn_cons, FreshState, bind, Except.bind, pure, Except.pure] <;>
                (intro impossible; cases impossible)
      | nonmovingOriginal marked => cases marked <;>
          simp_all [List.forIn_cons, FreshState, bind, Except.bind, pure, Except.pure] <;>
                (intro impossible; cases impossible)
      | ordinaryDestination | nonmovingDestination =>
          simp_all [List.forIn_cons, FreshState, bind, Except.bind, pure, Except.pure] <;>
                (intro impossible; cases impossible)

/-- Decode a logical ordinal independently of physical allocation order. -/
def logicalOrdinal (id : ObjectId) : Nat :=
  (String.ofList (id.value.toList.drop 4)).toNat?.getD 0

/-- Canonical obj-N wire identity; its ordinal is unrelated to the allocation ID. -/
def CanonicalObjectId (id : ObjectId) : Prop :=
  let digits := String.ofList (id.value.toList.drop 4)
  id.value.startsWith "obj-" = true ∧ digits.isEmpty = false ∧
    digits.toList.all (fun c => '0' ≤ c && c ≤ '9') = true ∧
    ¬(digits.length > 1 ∧ digits.startsWith "0" = true) ∧
    digits.toNat? = some (logicalOrdinal id) ∧ logicalOrdinal id ≤ 18446744073709551615

structure FreshIds (heap : HeapSnapshot) : Prop where
  allocations : heap.objects.toList.map (fun a => a.allocationId.value) = List.range heap.objects.size
  canonical : ∀ a ∈ heap.objects.toList, CanonicalObjectId a.objectId
  objects : (heap.objects.toList.map (fun a => logicalOrdinal a.objectId)).Perm (List.range heap.objects.size)

private theorem objectNumber_correct (id : ObjectId) (path : String) (number : Nat)
    (accepted : objectNumber id path = .ok number) :
    CanonicalObjectId id ∧ logicalOrdinal id = number := by
  unfold objectNumber at accepted
  generalize prefixEq : id.value.startsWith "obj-" = starts at accepted
  generalize suffix : String.ofList (id.value.toList.drop 4) = digits at accepted
  generalize decimal : (!digits.isEmpty && digits.toList.all (fun c => '0' ≤ c && c ≤ '9')) = valid at accepted
  generalize leading : (digits.length > 1 && digits.startsWith "0") = zero at accepted
  generalize parsed : digits.toNat? = parsedValue at accepted
  cases starts <;> cases valid <;> cases zero <;> cases parsedValue <;>
    simp only [Bool.false_eq_true, ↓reduceIte, bind, Except.bind, pure, Except.pure] at accepted
  all_goals
    repeat' first | contradiction | split at accepted
    all_goals
      cases accepted
      have bounded : ¬ number > 18446744073709551615 := by assumption
      simp only [CanonicalObjectId, logicalOrdinal, prefixEq, suffix, parsed, Option.getD_some]
      grind

theorem FreshState.not_destination (fresh : FreshState state) : isDestination state = false := by
  cases state with
  | youngOriginal policy header | oldOriginal policy header => rfl
  | nonmovingOriginal marked => rfl
  | ordinaryDestination | nonmovingDestination => cases fresh

private theorem validateAllocation_fresh (count originalCount : Nat) (before after : AllocationTables)
    (a : Allocation) (index : Nat) (fresh : FreshState a.state)
    (accepted : validateAllocation count originalCount before (a, index) = .ok after) :
    a.allocationId.value = index ∧ CanonicalObjectId a.objectId ∧
    ∃ h : logicalOrdinal a.objectId < before.origins.size,
      before.origins[logicalOrdinal a.objectId].isSome = false ∧
      after.origins = before.origins.set (logicalOrdinal a.objectId) (some a) := by
  have notDestination := FreshState.not_destination fresh
  unfold validateAllocation at accepted
  simp only [notDestination, Bool.false_eq_true, ↓reduceIte, bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    have parsed := objectNumber_correct _ _ _ (by assumption)
    obtain ⟨canonical, ordinal⟩ := parsed
    refine ⟨?_, canonical, ?_⟩
    · simpa using (show (a.allocationId.value == index) = true from by assumption)
    · subst_vars
      refine ⟨by assumption, ?_, rfl⟩
      simpa only [Bool.not_eq_true] using
        (show ¬(before.origins[logicalOrdinal a.objectId]'(by assumption)).isSome = true from by assumption)

structure IdentityProgress (bound : Nat) (processed : List (Allocation × Nat))
    (tables : AllocationTables) : Prop where
  size : tables.origins.size = bound
  allocations : ∀ pair ∈ processed, pair.1.allocationId.value = pair.2
  canonical : ∀ pair ∈ processed, CanonicalObjectId pair.1.objectId
  nodup : (processed.map (fun pair => logicalOrdinal pair.1.objectId)).Nodup
  bounded : ∀ pair ∈ processed, logicalOrdinal pair.1.objectId < bound
  occupied : ∀ i (valid : i < tables.origins.size),
    tables.origins[i].isSome = true ↔ i ∈ processed.map (fun pair => logicalOrdinal pair.1.objectId)

theorem IdentityProgress.step (progress : IdentityProgress bound processed before)
    (fresh : FreshState a.state)
    (accepted : validateAllocation count originalCount before (a, index) = .ok after) :
    IdentityProgress bound (processed ++ [(a, index)]) after := by
  obtain ⟨allocation, canonical, valid, empty, updated⟩ := validateAllocation_fresh _ _ _ _ _ _ fresh accepted
  have absent : logicalOrdinal a.objectId ∉ processed.map (fun pair => logicalOrdinal pair.1.objectId) := by
    intro present
    have occupied := (progress.occupied (logicalOrdinal a.objectId) valid).mpr present
    simp [empty] at occupied
  refine ⟨by simp [updated, progress.size], ?_, ?_, ?_, ?_, ?_⟩
  · intro pair member
    rcases List.mem_append.mp member with old | new
    · exact progress.allocations pair old
    · simp only [List.mem_singleton] at new
      subst pair
      exact allocation
  · intro pair member
    rcases List.mem_append.mp member with old | new
    · exact progress.canonical pair old
    · simp only [List.mem_singleton] at new
      subst pair
      exact canonical
  · rw [List.map_append, List.map_singleton, List.nodup_append]
    refine ⟨progress.nodup, by simp, ?_⟩
    intro value inside other member
    simp only [List.mem_singleton] at member
    subst other
    intro same
    exact absent (same ▸ inside)
  · intro pair member
    rcases List.mem_append.mp member with old | new
    · exact progress.bounded pair old
    · simp only [List.mem_singleton] at new
      subst pair
      simpa [progress.size] using valid
  · intro i inAfter
    have inBefore : i < before.origins.size := by simpa [updated] using inAfter
    by_cases same : logicalOrdinal a.objectId = i
    · simp [updated, same]
    · simpa only [updated, Array.getElem_set, same, ↓reduceIte, List.map_append,
        List.map_singleton, List.mem_append, List.mem_singleton, Ne.symm same,
        or_false] using progress.occupied i inBefore

private theorem validateAllocations_progress (pairs : List (Allocation × Nat))
    (progress : IdentityProgress bound processed before)
    (fresh : ∀ pair ∈ pairs, FreshState pair.1.state)
    (accepted : pairs.foldlM (validateAllocation count originalCount) before = .ok after) :
    IdentityProgress bound (processed ++ pairs) after := by
  induction pairs generalizing before processed with
  | nil =>
      simp [List.foldlM_nil, pure, Except.pure] at accepted
      subst after
      simpa using progress
  | cons pair pairs ih =>
      cases step : validateAllocation count originalCount before pair with
      | error message => simp [List.foldlM_cons, step, bind, Except.bind] at accepted
      | ok next =>
          simp only [List.foldlM_cons, step, bind, Except.bind] at accepted
          have progressed := IdentityProgress.step progress (fresh pair (by simp)) step
          simpa [List.append_assoc] using ih progressed (fun pair h => fresh pair (by simp [h])) accepted

theorem fresh_originalCount (objects : List Allocation)
    (fresh : ∀ a ∈ objects, FreshState a.state) (start : Nat) :
    objects.foldl (fun n a => if isDestination a.state then n else n + 1) start = start + objects.length := by
  induction objects generalizing start with
  | nil => simp
  | cons a rest ih =>
      have original := FreshState.not_destination (fresh a (by simp))
      simp only [List.foldl_cons, original, Bool.false_eq_true, ↓reduceIte]
      rw [ih (fun a h => fresh a (by simp [h]))]
      simp [Nat.add_comm, Nat.add_left_comm]

private theorem nodup_perm_range (numbers : List Nat) (nodup : numbers.Nodup)
    (bounded : ∀ number ∈ numbers, number < numbers.length) : numbers.Perm (List.range numbers.length) := by
  apply (List.perm_ext_iff_of_nodup nodup (List.nodup_range)).mpr
  intro number
  constructor
  · exact fun h => List.mem_range.mpr (bounded number h)
  · intro member
    by_cases missing : number ∈ numbers
    · exact missing
    exfalso
    have subset : numbers ⊆ (List.range numbers.length).erase number := by
      intro value inside
      apply (List.mem_erase_of_ne (by intro same; subst value; exact missing inside)).mpr
      exact List.mem_range.mpr (bounded value inside)
    have length := nodup.length_le_of_subset subset
    have erased := List.length_erase_of_mem member
    simp only [List.length_range] at erased
    have positive := List.mem_range.mp member
    omega

theorem IdentityProgress.empty (bound : Nat) (copies : Array (Option Allocation)) (seen : Std.HashSet Nat) :
    IdentityProgress bound [] ⟨Array.replicate bound none, copies, seen⟩ := by
  constructor <;> simp

private theorem IdentityProgress.fresh_ids (heap : HeapSnapshot)
    (progress : IdentityProgress heap.objects.size heap.objects.toList.zipIdx tables) : FreshIds heap := by
  constructor
  · have mapped := List.map_congr_left progress.allocations
    have lhs : heap.objects.toList.zipIdx.map (fun pair => pair.1.allocationId.value) =
        heap.objects.toList.map (fun a => a.allocationId.value) := by
      simpa only [List.map_map, Function.comp_def] using
        congrArg (List.map (fun a => a.allocationId.value)) (List.zipIdx_map_fst 0 heap.objects.toList)
    rw [lhs, List.zipIdx_map_snd] at mapped
    simpa [List.range_eq_range'] using mapped
  · intro a member
    rw [← List.zipIdx_map_fst 0 heap.objects.toList] at member
    obtain ⟨pair, inside, same⟩ := List.mem_map.mp member
    subst a
    exact progress.canonical pair inside
  · have mapped : heap.objects.toList.zipIdx.map (fun pair => logicalOrdinal pair.1.objectId) =
        heap.objects.toList.map (fun a => logicalOrdinal a.objectId) := by
      simpa only [List.map_map, Function.comp_def] using
        congrArg (List.map (fun a => logicalOrdinal a.objectId)) (List.zipIdx_map_fst 0 heap.objects.toList)
    have nodup := progress.nodup
    rw [mapped] at nodup
    have bounded : ∀ number ∈ heap.objects.toList.map (fun a => logicalOrdinal a.objectId),
        number < (heap.objects.toList.map (fun a => logicalOrdinal a.objectId)).length := by
      intro number member
      rw [← mapped] at member
      obtain ⟨pair, inside, same⟩ := List.mem_map.mp member
      simpa [← same] using progress.bounded pair inside
    simpa using nodup_perm_range _ nodup bounded

/-- Successful structural checking certifies both dense ID schemes for fresh heaps. -/
theorem validateStructure_fresh_ids (heap : HeapSnapshot)
    (accepted : validateStructure heap = .ok ()) (fresh : FreshStates heap) : FreshIds heap := by
  have count := fresh_originalCount heap.objects.toList fresh 0
  simp only [Nat.zero_add, Array.length_toList, Array.foldl_toList] at count
  unfold validateStructure at accepted
  simp only [count, bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    have progress := validateAllocations_progress heap.objects.toList.zipIdx
      (IdentityProgress.empty heap.objects.size _ _)
      (fun pair member => fresh pair.1 (List.fst_mem_of_mem_zipIdx member)) (by assumption)
    apply IdentityProgress.fresh_ids heap
    simpa using progress

/-- Preparation returns only normal, unmarked original states. -/
theorem prepareFreshHeap_states (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) : FreshStates heap := by
  obtain ⟨checked, validated, same⟩ := prepareFreshHeap_checked input heap accepted
  unfold prepareFreshHeap at accepted
  simp only [validated, bind, Except.bind] at accepted
  cases stateCheck : checkFreshHeap checked.val with
  | error message => simp [stateCheck] at accepted
  | ok value => cases value; rw [← same]; exact (checkFreshHeap_correct _).mp stateCheck

/-- Fresh preparation returns ordered dense allocations and canonical dense logical identities. -/
theorem prepareFreshHeap_ids (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) : FreshIds heap := by
  obtain ⟨checked, validated, same⟩ := prepareFreshHeap_checked input heap accepted
  have fresh := prepareFreshHeap_states input heap accepted
  apply validateStructure_fresh_ids heap ?_ fresh
  rw [← same]
  exact validateAndOrder_structure input checked validated

/-- Scalar values are opaque; only non-null reference targets impose a bound. -/
def CellReferencesValid (count : Nat) : Cell → Prop
  | .immediate _ => True
  | .reference target => ReferenceValid count target

def WeakContentsReferencesValid (count : Nat) : WeakContents → Prop
  | .field value => CellReferencesValid count value
  | .entry entry => ReferenceValid count entry.firstGuard ∧
      ReferenceValid count entry.secondGuard ∧ CellReferencesValid count entry.payload

def AllocationCellReferencesValid (count : Nat) (allocation : Allocation) : Prop :=
  (∀ cell ∈ allocation.strong.toList, CellReferencesValid count cell) ∧
  (∀ unit ∈ allocation.weak.toList, WeakContentsReferencesValid count unit.contents)

/-- Every stored cell/guard target names an allocation, including inactive entries.
    Forwarding headers and remembered-owner IDs have separate validation contracts. -/
structure HeapCellReferencesValid (heap : HeapSnapshot) : Prop where
  roots : ∀ cell ∈ heap.roots.toList, CellReferencesValid heap.objects.size cell
  objects : ∀ allocation ∈ heap.objects.toList, AllocationCellReferencesValid heap.objects.size allocation
  externalWeak : ∀ unit ∈ heap.externalWeak.toList, WeakContentsReferencesValid heap.objects.size unit.contents

/-- The placeholder codec preserves the exact signed integer and accepts every Int64. -/
theorem decodeImmediate_correct (value : Int) (path : String) (encoded : Int64) :
    decodeImmediate value path = .ok encoded ↔ value = encoded.toInt := by
  constructor
  · intro accepted
    unfold decodeImmediate at accepted
    simp only [bind, Except.bind, pure, Except.pure] at accepted
    repeat' first | contradiction | split at accepted
    all_goals
      cases accepted
      have limits : value ≥ (-9223372036854775808 : Int) ∧ value ≤ (9223372036854775807 : Int) := by
        simpa only [Bool.and_eq_true, decide_eq_true_eq] using
          (show (value ≥ (-9223372036854775808 : Int) && value ≤ (9223372036854775807 : Int)) = true from by assumption)
      have lower : -2^63 ≤ value := by change (-9223372036854775808 : Int) ≤ value; omega
      have upper : value < 2^63 := by change value < (9223372036854775808 : Int); omega
      exact (Int64.toInt_ofInt_of_le lower upper).symm
  · intro same
    subst value
    have lower : encoded.toInt ≥ (-9223372036854775808 : Int) := encoded.le_toInt
    have upper : encoded.toInt ≤ (9223372036854775807 : Int) := by
      have bound := encoded.toInt_lt
      change encoded.toInt < (9223372036854775808 : Int) at bound
      omega
    unfold decodeImmediate
    simp only [bind, Except.bind, pure, Except.pure]
    split
    · simp only [Int64.ofInt_toInt]
    · rename_i invalid
      exfalso
      apply invalid
      change (encoded.toInt ≥ (-9223372036854775808 : Int) && encoded.toInt ≤ (9223372036854775807 : Int)) = true
      simp [lower, upper]

/-- Cell checking is exactly reference validation; every immediate is representable by type. -/
theorem checkCell_correct (value : Cell) (count : Nat) (path : String) :
    checkCell value count path = .ok () ↔ CellReferencesValid count value := by
  cases value with
  | immediate value => simp [checkCell, CellReferencesValid, pure, Except.pure]
  | reference target => exact checkReference_correct _ _ _

/-- Successful cell checking certifies reference bounds without constraining scalar values. -/
theorem checkCell_references (value : Cell) (count : Nat) (path : String)
    (accepted : checkCell value count path = .ok ()) : CellReferencesValid count value := by
  exact (checkCell_correct _ _ _).mp accepted

/-- A successful loop certifies every element when successful steps always continue. -/
private theorem forIn_all_checked (xs : List α) (state final : β)
    (step : α → β → Except String (ForInStep β)) (property : α → Prop)
    (checked : ∀ a before result, step a before = .ok result →
      ∃ after, result = .yield after ∧ property a)
    (accepted : forIn xs state step = .ok final) : ∀ a ∈ xs, property a := by
  induction xs generalizing state with
  | nil => simp
  | cons a rest ih =>
      cases first : step a state with
      | error message => simp [List.forIn_cons, first, bind, Except.bind] at accepted
      | ok result =>
          obtain ⟨after, rfl, valid⟩ := checked a state result first
          simp only [List.forIn_cons, first, bind, Except.bind] at accepted
          intro value member
          rcases List.mem_cons.mp member with same | inside
          · exact same ▸ valid
          · exact ih after accepted value inside

private theorem foldlM_all_checked (xs : List α) (state final : β)
    (step : β → α → Except String β) (property : α → Prop)
    (checked : ∀ before a after, step before a = .ok after → property a)
    (accepted : xs.foldlM step state = .ok final) : ∀ a ∈ xs, property a := by
  induction xs generalizing state with
  | nil => simp
  | cons a rest ih =>
      cases first : step state a with
      | error message => simp [List.foldlM_cons, first, bind, Except.bind] at accepted
      | ok after =>
          simp only [List.foldlM_cons, first, bind, Except.bind] at accepted
          intro value member
          rcases List.mem_cons.mp member with same | inside
          · exact same ▸ checked state a after first
          · exact ih after accepted value inside

private theorem zipIdx_all (xs : List α) (property : α → Prop)
    (checked : ∀ pair ∈ xs.zipIdx, property pair.1) : ∀ a ∈ xs, property a := by
  intro a member
  rw [← List.zipIdx_map_fst 0 xs] at member
  obtain ⟨pair, inside, rfl⟩ := List.mem_map.mp member
  exact checked pair inside

private theorem checkCells_references (cells : Array Cell) (count : Nat) (path : Nat → String)
    (final : PUnit)
    (accepted : forIn cells.toList.zipIdx PUnit.unit (fun (pair : Cell × Nat) (_ : PUnit) => do
      checkCell pair.1 count (path pair.2)
      pure (.yield PUnit.unit)) = .ok final) :
    ∀ cell ∈ cells.toList, CellReferencesValid count cell := by
  apply zipIdx_all cells.toList (CellReferencesValid count)
  apply forIn_all_checked _ _ _ _ (fun pair : Cell × Nat => CellReferencesValid count pair.1) ?_ accepted
  intro pair before output succeeded
  simp only [bind, Except.bind, pure, Except.pure] at succeeded
  repeat' first | contradiction | split at succeeded
  all_goals
    cases succeeded
    exact ⟨_, rfl, checkCell_references _ _ _ (by assumption)⟩

def WeakContents.isField : WeakContents → Bool
  | .field _ => true
  | .entry _ => false

private theorem unit_bind_ok (action : Except String Unit) (next : Except String α) (value : α) :
    (action >>= fun _ => next) = .ok value ↔ action = .ok () ∧ next = .ok value := by
  cases action with
  | error message => simp [bind, Except.bind]
  | ok result => cases result; simp [bind, Except.bind]

private theorem unit_map_ok (action : Except String Unit) (next value : α) :
    (fun _ => next) <$> action = .ok value ↔ action = .ok () ∧ next = value := by
  cases action with
  | error message => simp [Functor.map, Except.map]
  | ok result => cases result; simp [Functor.map, Except.map]

/-- Exact per-unit contract: all cells are valid, owned fields precede entries,
    and only original/external storage inserts a fresh logical weak ID. -/
theorem checkUnit_correct (path : String) (storage : UnitStorage) (count : Nat)
    (before after : Std.HashSet Nat × Bool) (pair : WeakUnit × Nat) :
    checkUnit path storage count before pair = .ok after ↔
      WeakContentsReferencesValid count pair.1.contents ∧
      (storage ≠ .destination → before.1.contains pair.1.unitId.value = false) ∧
      (pair.1.contents.isField = true → storage = .external ∨ before.2 = false) ∧
      after = (if storage = .destination then before.1 else before.1.insert pair.1.unitId.value,
        before.2 || !pair.1.contents.isField) := by
  cases duplicate : before.1.contains pair.1.unitId.value <;> cases flag : before.2 <;>
    cases storage <;> cases contents : pair.1.contents <;>
      simp [checkUnit, contents, duplicate, flag, WeakContentsReferencesValid, WeakContents.isField,
        unit_bind_ok, unit_map_ok, checkCell_correct, checkReference_correct] <;>
        first
        | (intro impossible
           change (Except.error _ : Except String Unit) = .ok () at impossible
           cases impossible)
        | simp [and_assoc, eq_comm]

/-- All unit contents are checked, even guards or payloads of inactive entries. -/
theorem checkUnits_references (values : Array WeakUnit) (path : String) (storage : UnitStorage)
    (count : Nat) (seen result : Std.HashSet Nat)
    (accepted : checkUnits values path storage count seen = .ok result) :
    ∀ unit ∈ values.toList, WeakContentsReferencesValid count unit.contents := by
  unfold checkUnits at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    apply zipIdx_all values.toList (fun unit => WeakContentsReferencesValid count unit.contents)
    exact foldlM_all_checked _ _ _ _ _
      (fun before pair after checked => ((checkUnit_correct _ _ _ before after pair).mp checked).1) (by assumption)

private theorem validateAllocation_cell_references (count originalCount : Nat) (before : AllocationTables)
    (pair : Allocation × Nat) (after : AllocationTables)
    (accepted : validateAllocation count originalCount before pair = .ok after) :
    AllocationCellReferencesValid count pair.1 := by
  unfold validateAllocation at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    cases accepted
    exact ⟨checkCells_references _ _ _ _ (by assumption), checkUnits_references _ _ _ _ _ _ (by assumption)⟩

/-- Successful structure validation bounds all cell and guard references, for
    both fresh heaps and completed snapshots. -/
theorem validateStructure_cell_references (heap : HeapSnapshot)
    (accepted : validateStructure heap = .ok ()) : HeapCellReferencesValid heap := by
  unfold validateStructure at accepted
  simp only [bind, Except.bind, pure, Except.pure] at accepted
  repeat' first | contradiction | split at accepted
  all_goals
    refine ⟨checkCells_references _ _ _ _ (by assumption), ?_,
      checkUnits_references _ _ _ _ _ _ (by assumption)⟩
    apply zipIdx_all heap.objects.toList (AllocationCellReferencesValid heap.objects.size)
    exact foldlM_all_checked _ _ _ _ _
      (validateAllocation_cell_references _ _) (by assumption)

/-- Fresh preparation certifies all stored cell/guard targets in its returned heap. -/
theorem prepareFreshHeap_cell_references (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) : HeapCellReferencesValid heap := by
  obtain ⟨checked, validated, same⟩ := prepareFreshHeap_checked input heap accepted
  rw [← same]
  exact validateStructure_cell_references checked.val (validateAndOrder_structure input checked validated)


end CloverGC.Differential.Format
