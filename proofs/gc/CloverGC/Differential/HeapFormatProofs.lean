import CloverGC.Differential.HeapFormat
import Init.Data.Array.Sort.Lemmas
import Init.Data.SInt.Lemmas

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

/-- Canonical decimal syntax; the ordinal itself is unrelated to the allocation ID. -/
def CanonicalObjectId (id : ObjectId) : Prop :=
  let digits := String.ofList (id.value.toList.drop 4)
  id.value.startsWith "obj-" = true ∧ digits.isEmpty = false ∧
    digits.toList.all (fun c => '0' ≤ c && c ≤ '9') = true ∧
    ¬(digits.length > 1 ∧ digits.startsWith "0" = true) ∧
    digits.toNat? = some (logicalOrdinal id)

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
      simp only [CanonicalObjectId, logicalOrdinal, prefixEq, suffix, parsed, Option.getD_some]
      grind

private theorem FreshState.not_destination (fresh : FreshState state) : isDestination state = false := by
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

private structure IdentityProgress (bound : Nat) (processed : List (Allocation × Nat))
    (tables : AllocationTables) : Prop where
  size : tables.origins.size = bound
  allocations : ∀ pair ∈ processed, pair.1.allocationId.value = pair.2
  canonical : ∀ pair ∈ processed, CanonicalObjectId pair.1.objectId
  nodup : (processed.map (fun pair => logicalOrdinal pair.1.objectId)).Nodup
  bounded : ∀ pair ∈ processed, logicalOrdinal pair.1.objectId < bound
  occupied : ∀ i (valid : i < tables.origins.size),
    tables.origins[i].isSome = true ↔ i ∈ processed.map (fun pair => logicalOrdinal pair.1.objectId)

private theorem IdentityProgress.step (progress : IdentityProgress bound processed before)
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

private theorem fresh_originalCount (objects : List Allocation)
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

private theorem IdentityProgress.empty (bound : Nat) (copies : Array (Option Allocation)) (seen : Std.HashSet Nat) :
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

/-- Fresh preparation returns ordered dense allocations and canonical dense logical identities. -/
theorem prepareFreshHeap_ids (input heap : HeapSnapshot)
    (accepted : prepareFreshHeap input = .ok heap) : FreshIds heap := by
  obtain ⟨checked, validated, same⟩ := prepareFreshHeap_checked input heap accepted
  have ordered := validateAndOrder_value input checked validated
  have fresh : FreshStates heap := by
    unfold prepareFreshHeap at accepted
    simp only [validated, bind, Except.bind] at accepted
    cases stateCheck : checkFreshHeap checked.val with
    | error message => simp [stateCheck] at accepted
    | ok value =>
        cases value
        rw [← same]
        exact (checkFreshHeap_correct _).mp stateCheck
  apply validateStructure_fresh_ids heap ?_ fresh
  unfold validateAndOrder at validated
  cases structureCheck : validateStructure (orderedHeap input) with
  | error message => simp [structureCheck, bind, Except.bind] at validated
  | ok value =>
      cases value
      simpa [← same, ordered] using structureCheck
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


end CloverGC.Differential.Format
