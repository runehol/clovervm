import CloverGC.Differential.HeapObjectPairCompleteness

namespace CloverGC.Differential.Format

def originalUnitIds (heap : HeapSnapshot) (originals : List Allocation) : List Nat :=
  (heap.externalWeak.toList ++ originals.flatMap (fun a => a.weak.toList)).map (fun unit => unit.unitId.value)

/-- Semantic structure of an allocation-ordered snapshot, including copied records.
    The source and copy lists are proof views of one global allocation-ID space. -/
structure CompletedWellFormed (heap : HeapSnapshot) (originals copies : List Allocation) : Prop where
  records : heap.objects.toList = originals ++ copies
  originalStates : ∀ a ∈ originals, isDestination a.state = false
  destinationStates : ∀ a ∈ copies, isDestination a.state = true
  allocations : (heap.objects.toList.map (fun a => a.allocationId.value)) = List.range heap.objects.size
  canonical : ∀ a ∈ heap.objects.toList, CanonicalObjectId a.objectId
  originalIds : (originals.map (fun a => logicalOrdinal a.objectId)).Perm (List.range originals.length)
  copyIds : (copies.map (fun a => logicalOrdinal a.objectId)).Nodup
  copyBounds : ∀ a ∈ copies, logicalOrdinal a.objectId < originals.length
  references : HeapCellReferencesValid heap
  ownerLayout : ∀ a ∈ heap.objects.toList, FieldsBeforeEntries a.weak.toList
  units : (originalUnitIds heap originals).Perm (List.range (originalUnitIds heap originals).length)
  remembered : RememberedValid heap
  pairs : ∀ a ∈ originals, AllocationPairValid a (copies.find? (fun b => logicalOrdinal b.objectId == logicalOrdinal a.objectId))

/-- Destination storage reuses logical weak IDs without inserting them again. -/
private theorem checkDestinationUnit_complete (path : String) (count : Nat)
    (before : Std.HashSet Nat × Bool) (pair : WeakUnit × Nat)
    (content : WeakContentsReferencesValid count pair.1.contents)
    (layout : pair.1.contents.isField = true → before.2 = false) :
    checkUnit path .destination count before pair =
      .ok (before.1, before.2 || !pair.1.contents.isField) := by
  apply (checkUnit_correct _ _ _ _ _ _).mpr
  exact ⟨content, by simp, fun field => Or.inr (layout field), by simp⟩

private theorem checkDestinationUnitList_complete (pairs : List (WeakUnit × Nat))
    (before : Std.HashSet Nat × Bool) (processed : List WeakUnit)
    (flag : before.2 = processed.any (fun unit => !unit.contents.isField))
    (references : ∀ pair ∈ pairs, WeakContentsReferencesValid count pair.1.contents)
    (layout : FieldsBeforeEntries (processed ++ pairs.map Prod.fst)) :
    ∃ after, pairs.foldlM (checkUnit path .destination count) before = .ok after ∧
      after.1 = before.1 := by
  induction pairs generalizing before processed with
  | nil => exact ⟨before, rfl, rfl⟩
  | cons pair rest ih =>
      have clear : pair.1.contents.isField = true → before.2 = false := by
        intro field
        rw [flag]
        apply List.any_eq_false.mpr
        intro unit member
        have prior := (List.pairwise_append.mp layout).2.2 unit member pair.1 (by simp) field
        simp [prior]
      let next := (before.1, before.2 || !pair.1.contents.isField)
      have step : checkUnit path .destination count before pair = .ok next :=
        checkDestinationUnit_complete _ _ _ _ (references pair (by simp)) clear
      obtain ⟨after, done, preserved⟩ := ih next (processed ++ [pair.1])
        (by simp [next, flag]) (fun pair member => references pair (by simp [member]))
        (by simpa [List.append_assoc] using layout)
      exact ⟨after, by simpa only [List.foldlM_cons, step, bind, Except.bind] using done, preserved⟩

/-- Destination unit validation preserves the existing logical-ID ledger exactly. -/
private theorem checkDestinationUnits_complete (values : Array WeakUnit) (path : String)
    (count : Nat) (seen : Std.HashSet Nat)
    (references : ∀ unit ∈ values.toList, WeakContentsReferencesValid count unit.contents)
    (layout : FieldsBeforeEntries values.toList) :
    checkUnits values path .destination count seen = .ok seen := by
  obtain ⟨after, done, preserved⟩ := checkDestinationUnitList_complete
    (path := path) values.toList.zipIdx (seen, false) [] rfl
    (fun pair member => references pair.1 (List.fst_mem_of_mem_zipIdx member))
    (by simpa only [List.nil_append, List.zipIdx_map_fst] using layout)
  simp [checkUnits, done, Functor.map, Except.map, preserved]

/-- A destination extends only its copy slot; source records and weak IDs are unchanged. -/
private theorem validateDestinationAllocation_success (count originalCount : Nat) (before : AllocationTables)
    (pair : Allocation × Nat)
    (destination : isDestination pair.1.state = true)
    (physical : pair.1.allocationId.value = pair.2)
    (afterOriginals : originalCount ≤ pair.1.allocationId.value)
    (canonical : CanonicalObjectId pair.1.objectId)
    (slot : logicalOrdinal pair.1.objectId < before.copies.size)
    (emptySlot : before.copies[logicalOrdinal pair.1.objectId] = none)
    (references : AllocationCellReferencesValid count pair.1)
    (layout : FieldsBeforeEntries pair.1.weak.toList) :
    validateAllocation count originalCount before pair = .ok
      ⟨before.origins, before.copies.set (logicalOrdinal pair.1.objectId) (some pair.1), before.seen⟩ := by
  have parsed := objectNumber_complete pair.1.objectId
    (indexPath "$.objects" pair.2 ++ ".object_id") canonical
  have strong := checkCells_success pair.1.strong count
    (fun index => indexPath (indexPath "$.objects" pair.2 ++ ".strong") index) references.1
  simp only [bind, Except.bind, pure, Except.pure] at strong
  have weak := checkDestinationUnits_complete pair.1.weak
    (indexPath "$.objects" pair.2 ++ ".weak") count before.seen references.2 layout
  have indexBound : originalCount ≤ pair.2 := by simpa only [physical] using afterOriginals
  simp [validateAllocation, destination, physical, indexBound, parsed, slot, emptySlot,
    strong, weak, bind, Except.bind, pure, Except.pure]

/-- Exact sparse indexing of allocation records by their logical object IDs. -/
private structure AllocationSlots (bound : Nat) (records : List Allocation)
    (slots : Array (Option Allocation)) : Prop where
  size : slots.size = bound
  unique : (records.map (fun a => logicalOrdinal a.objectId)).Nodup
  bounded : ∀ a ∈ records, logicalOrdinal a.objectId < bound
  lookup : ∀ i (within : i < slots.size),
    slots[i] = records.find? (fun a => logicalOrdinal a.objectId == i)

private theorem AllocationSlots.empty (bound : Nat) :
    AllocationSlots bound [] (Array.replicate bound none) := by
  constructor
  · simp
  · simp
  · simp
  · intro i within
    simp

private theorem findAllocation_absent (records : List Allocation) (index : Nat)
    (absent : index ∉ records.map (fun a => logicalOrdinal a.objectId)) :
    records.find? (fun a => logicalOrdinal a.objectId == index) = none := by
  apply List.find?_eq_none.mpr
  intro a member equal
  exact absent (List.mem_map.mpr ⟨a, member, by simpa using equal⟩)

/-- Installing a fresh logical ID preserves exact lookup of every other slot. -/
private theorem AllocationSlots.insert (progress : AllocationSlots bound records slots)
    (a : Allocation) (within : logicalOrdinal a.objectId < bound)
    (absent : logicalOrdinal a.objectId ∉ records.map (fun a => logicalOrdinal a.objectId)) :
    AllocationSlots bound (records ++ [a])
      (slots.set (logicalOrdinal a.objectId) (some a) (by simpa [progress.size] using within)) := by
  refine ⟨by simp [progress.size], ?_, ?_, ?_⟩
  · rw [List.map_append, List.map_singleton, List.nodup_append]
    refine ⟨progress.unique, by simp, ?_⟩
    intro i inside j member same
    simp only [List.mem_singleton] at member
    exact absent (member ▸ same ▸ inside)
  · intro record member
    rcases List.mem_append.mp member with old | new
    · exact progress.bounded record old
    · have same : record = a := List.mem_singleton.mp new
      exact same ▸ within
  · intro i valid
    have before : i < slots.size := by simpa using valid
    have original := progress.lookup i before
    by_cases same : logicalOrdinal a.objectId = i
    · have vacant := findAllocation_absent records _ (same ▸ absent)
      simp [same, List.find?_append, vacant]
    · have different : (logicalOrdinal a.objectId == i) = false := by simp [same]
      simpa [Array.getElem_set, same, List.find?_append, different] using original


/-- Every unprocessed logical ID is an empty slot, independently of the validator. -/
private theorem AllocationSlots.vacant (progress : AllocationSlots bound records slots)
    (index : Nat) (within : index < slots.size)
    (absent : index ∉ records.map (fun a => logicalOrdinal a.objectId)) :
    slots[index] = none := (progress.lookup index within).trans (findAllocation_absent records index absent)

/-- Completed original records accept marks and forwarding while retaining exact
    source indexing, the copy table and the global logical weak-unit ledger. -/
private theorem validateOriginalRecords_success (pairs : List (Allocation × Nat))
    (before : AllocationTables) (processed : List Allocation) (ids : List Nat)
    (origins : AllocationSlots bound processed before.origins) (ledger : IdLedger ids before.seen)
    (records : ∀ pair ∈ pairs,
      isDestination pair.1.state = false ∧ pair.1.allocationId.value = pair.2 ∧ pair.2 < bound ∧
      CanonicalObjectId pair.1.objectId ∧ logicalOrdinal pair.1.objectId < bound ∧
      AllocationCellReferencesValid count pair.1 ∧ FieldsBeforeEntries pair.1.weak.toList)
    (uniqueObjects : ((processed ++ pairs.map Prod.fst).map (fun a => logicalOrdinal a.objectId)).Nodup)
    (uniqueUnits : (ids ++ pairs.flatMap (fun pair => pair.1.weak.toList.map (fun unit => unit.unitId.value))).Nodup) :
    ∃ after, pairs.foldlM (validateAllocation count bound) before = .ok after ∧
      AllocationSlots bound (processed ++ pairs.map Prod.fst) after.origins ∧ after.copies = before.copies ∧
      IdLedger (ids ++ pairs.flatMap (fun pair => pair.1.weak.toList.map (fun unit => unit.unitId.value))) after.seen := by
  induction pairs generalizing before processed ids with
  | nil => exact ⟨before, rfl, by simpa using origins, rfl, by simpa using ledger⟩
  | cons pair rest ih =>
      obtain ⟨original, physical, within, canonical, logical, references, layout⟩ := records pair (by simp)
      have slot : logicalOrdinal pair.1.objectId < before.origins.size := by simpa [origins.size] using logical
      have absent : logicalOrdinal pair.1.objectId ∉ processed.map (fun a => logicalOrdinal a.objectId) := by
        intro present
        exact (List.nodup_append.mp (by simpa only [List.map_append] using uniqueObjects)).2.2 _ present _ (by simp) rfl
      have headUnique : (ids ++ pair.1.weak.toList.map (fun unit => unit.unitId.value)).Nodup :=
        List.nodup_append.mp (by simpa [List.append_assoc] using uniqueUnits) |>.1
      obtain ⟨next, step, changed, copies, units⟩ := validateOriginalAllocation_success count bound before pair ids
        original physical (by simpa [physical] using within) canonical slot (origins.vacant _ slot absent)
        references layout ledger headUnique
      have nextOrigins : AllocationSlots bound (processed ++ [pair.1]) next.origins := by
        rw [changed]
        exact origins.insert pair.1 logical absent
      obtain ⟨after, done, indexed, preserved, final⟩ := ih next (processed ++ [pair.1])
        (ids ++ pair.1.weak.toList.map (fun unit => unit.unitId.value)) nextOrigins units
        (fun pair member => records pair (by simp [member]))
        (by simpa [List.append_assoc] using uniqueObjects)
        (by simpa [List.append_assoc] using uniqueUnits)
      refine ⟨after, ?_, ?_, preserved.trans copies, ?_⟩
      · simpa only [List.foldlM_cons, step, bind, Except.bind] using done
      · simpa [List.append_assoc] using indexed
      · simpa [List.append_assoc] using final

/-- Destination scanning fills exact copy slots without inserting shared unit IDs
    or changing the source table. -/
private theorem validateDestinationRecords_success (pairs : List (Allocation × Nat))
    (before : AllocationTables) (processed : List Allocation)
    (copies : AllocationSlots bound processed before.copies)
    (records : ∀ pair ∈ pairs,
      isDestination pair.1.state = true ∧ pair.1.allocationId.value = pair.2 ∧ bound ≤ pair.2 ∧
      CanonicalObjectId pair.1.objectId ∧ logicalOrdinal pair.1.objectId < bound ∧
      AllocationCellReferencesValid count pair.1 ∧ FieldsBeforeEntries pair.1.weak.toList)
    (uniqueObjects : ((processed ++ pairs.map Prod.fst).map (fun a => logicalOrdinal a.objectId)).Nodup) :
    ∃ after, pairs.foldlM (validateAllocation count bound) before = .ok after ∧
      AllocationSlots bound (processed ++ pairs.map Prod.fst) after.copies ∧
      after.origins = before.origins ∧ after.seen = before.seen := by
  induction pairs generalizing before processed with
  | nil => exact ⟨before, rfl, by simpa using copies, rfl, rfl⟩
  | cons pair rest ih =>
      obtain ⟨destination, physical, within, canonical, logical, references, layout⟩ := records pair (by simp)
      have slot : logicalOrdinal pair.1.objectId < before.copies.size := by simpa [copies.size] using logical
      have absent : logicalOrdinal pair.1.objectId ∉ processed.map (fun a => logicalOrdinal a.objectId) := by
        intro present
        exact (List.nodup_append.mp (by simpa only [List.map_append] using uniqueObjects)).2.2 _ present _ (by simp) rfl
      let next : AllocationTables := ⟨before.origins, before.copies.set (logicalOrdinal pair.1.objectId) (some pair.1) slot, before.seen⟩
      have step : validateAllocation count bound before pair = .ok next :=
        validateDestinationAllocation_success count bound before pair destination physical
          (by simpa [physical] using within) canonical slot (copies.vacant _ slot absent) references layout
      have indexed : AllocationSlots bound (processed ++ [pair.1]) next.copies :=
        copies.insert pair.1 logical absent
      obtain ⟨after, done, final, sources, seen⟩ := ih next (processed ++ [pair.1]) indexed
        (fun pair member => records pair (by simp [member]))
        (by simpa [List.append_assoc] using uniqueObjects)
      exact ⟨after, by simpa only [List.foldlM_cons, step, bind, Except.bind] using done,
        by simpa [List.append_assoc] using final, sources, seen⟩

private theorem countOriginals (records : List Allocation) (original : ∀ a ∈ records, isDestination a.state = false)
    (start : Nat) : records.foldl (fun n a => if isDestination a.state then n else n + 1) start = start + records.length := by
  induction records generalizing start with
  | nil => simp
  | cons a rest ih =>
      simp only [List.foldl_cons, original a (by simp), Bool.false_eq_true, ↓reduceIte]
      rw [ih (fun a member => original a (by simp [member]))]
      simp [Nat.add_comm, Nat.add_left_comm]

private theorem countDestinations (records : List Allocation) (destination : ∀ a ∈ records, isDestination a.state = true)
    (start : Nat) : records.foldl (fun n a => if isDestination a.state then n else n + 1) start = start := by
  induction records with
  | nil => rfl
  | cons a rest ih =>
      simp only [List.foldl_cons, destination a (by simp), ↓reduceIte]
      exact ih (fun a member => destination a (by simp [member]))

private theorem denseAllocation_position (heap : HeapSnapshot)
    (dense : heap.objects.toList.map (fun a => a.allocationId.value) = List.range heap.objects.size)
    (pair : Allocation × Nat) (member : pair ∈ heap.objects.toList.zipIdx) :
    pair.1.allocationId.value = pair.2 := by
  have position := List.mem_zipIdx member
  have within : pair.2 < heap.objects.toList.length := by simpa using position.2.1
  have owner : pair.1 = heap.objects.toList[pair.2] := by simpa using position.2.2
  have same := congrArg (fun ids : List Nat => ids[pair.2]?) dense
  simpa [List.getElem?_map, List.getElem?_eq_getElem within, owner,
    List.getElem?_range (by simpa using within)] using same

/-- Dense global physical IDs already put the snapshot in canonical allocation order. -/
theorem orderedHeap_eq_of_dense (heap : HeapSnapshot)
    (dense : heap.objects.toList.map (fun a => a.allocationId.value) = List.range heap.objects.size) :
    orderedHeap heap = heap := by
  have sorted : (heap.objects.toList.map (fun a => a.allocationId.value)).Pairwise (· ≤ ·) := by
    rw [dense]
    exact List.pairwise_le_range
  have paired : heap.objects.toList.Pairwise (fun a b => (a.allocationId.value ≤ b.allocationId.value : Bool)) := by
    simpa only [List.pairwise_map, decide_eq_true_eq] using sorted
  have unchanged := Array.mergeSort_of_pairwise paired
  simp only [orderedHeap, unchanged]

/-- All structural checks accept an independently valid completed snapshot. -/
theorem validateStructure_completed_complete (heap : HeapSnapshot) (originals copies : List Allocation)
    (valid : CompletedWellFormed heap originals copies) : validateStructure heap = .ok () := by
  let sourcePairs := originals.zipIdx
  let copyPairs := copies.zipIdx originals.length
  have partition : heap.objects.toList.zipIdx = sourcePairs ++ copyPairs := by
    simp only [valid.records, List.zipIdx_append, Nat.zero_add, sourcePairs, copyPairs]
  have memberSource (a : Allocation) (member : a ∈ originals) : a ∈ heap.objects.toList := by
    rw [valid.records]
    exact List.mem_append_left _ member
  have memberCopy (a : Allocation) (member : a ∈ copies) : a ∈ heap.objects.toList := by
    rw [valid.records]
    exact List.mem_append_right _ member
  have count : heap.objects.foldl (fun n a => if isDestination a.state then n else n + 1) 0 = originals.length := by
    rw [← Array.foldl_toList, valid.records, List.foldl_append,
      countOriginals originals valid.originalStates 0, countDestinations copies valid.destinationStates]
    simp
  have roots := checkCells_success heap.roots heap.objects.size (indexPath "$.roots") valid.references.roots
  simp only [bind, Except.bind, pure, Except.pure] at roots
  have unitKeys : heap.externalWeak.toList.map (fun u => u.unitId.value) ++
      sourcePairs.flatMap (fun pair => pair.1.weak.toList.map (fun u => u.unitId.value)) = originalUnitIds heap originals := by
    have keys := congrArg (List.flatMap (fun a : Allocation => a.weak.toList.map (fun u => u.unitId.value)))
      (List.zipIdx_map_fst 0 originals)
    have flattened : sourcePairs.flatMap (fun pair => pair.1.weak.toList.map (fun u => u.unitId.value)) =
        originals.flatMap (fun a => a.weak.toList.map (fun u => u.unitId.value)) := by
      simpa only [sourcePairs, List.flatMap_map, Function.comp_def] using keys
    simpa only [originalUnitIds, List.map_append, List.map_flatMap] using congrArg
      (fun ids => heap.externalWeak.toList.map (fun u => u.unitId.value) ++ ids) flattened
  have uniqueUnits := valid.units.symm.nodup List.nodup_range
  have externalUnique : (heap.externalWeak.toList.map (fun u => u.unitId.value)).Nodup := by
    rw [← unitKeys] at uniqueUnits
    exact (List.nodup_append.mp uniqueUnits).1
  obtain ⟨seen, externals, externalLedger⟩ := checkUnits_success heap.externalWeak "$.external_weak" .external
    heap.objects.size {} [] IdLedger.empty (by decide) valid.references.externalWeak (Or.inl rfl)
    (by simpa using externalUnique)
  have sourceRecords : ∀ pair ∈ sourcePairs,
      isDestination pair.1.state = false ∧ pair.1.allocationId.value = pair.2 ∧ pair.2 < originals.length ∧
      CanonicalObjectId pair.1.objectId ∧ logicalOrdinal pair.1.objectId < originals.length ∧
      AllocationCellReferencesValid heap.objects.size pair.1 ∧ FieldsBeforeEntries pair.1.weak.toList := by
    intro pair member
    have inside := List.fst_mem_of_mem_zipIdx member
    have present := memberSource pair.1 inside
    exact ⟨valid.originalStates _ inside,
      denseAllocation_position heap valid.allocations pair (by rw [partition]; exact List.mem_append_left _ member),
      (List.mem_zipIdx' member).1, valid.canonical _ present,
      List.mem_range.mp (valid.originalIds.mem_iff.mp (List.mem_map.mpr ⟨pair.1, inside, rfl⟩)),
      valid.references.objects _ present, valid.ownerLayout _ present⟩
  obtain ⟨sourceTable, sourcesChecked, indexedSources, unchangedCopies, sourceLedger⟩ :=
    validateOriginalRecords_success sourcePairs
      ⟨Array.replicate originals.length none, Array.replicate originals.length none, seen⟩ [] _
      (AllocationSlots.empty _) (by simpa using externalLedger) sourceRecords
      (by simpa [sourcePairs, List.zipIdx_map_fst] using valid.originalIds.symm.nodup List.nodup_range)
      (by rw [unitKeys]; exact uniqueUnits)
  have sourceIndex : AllocationSlots originals.length originals sourceTable.origins := by
    simpa [sourcePairs, List.zipIdx_map_fst] using indexedSources
  have copyRecords : ∀ pair ∈ copyPairs,
      isDestination pair.1.state = true ∧ pair.1.allocationId.value = pair.2 ∧ originals.length ≤ pair.2 ∧
      CanonicalObjectId pair.1.objectId ∧ logicalOrdinal pair.1.objectId < originals.length ∧
      AllocationCellReferencesValid heap.objects.size pair.1 ∧ FieldsBeforeEntries pair.1.weak.toList := by
    intro pair member
    have inside := List.fst_mem_of_mem_zipIdx member
    have present := memberCopy pair.1 inside
    exact ⟨valid.destinationStates _ inside,
      denseAllocation_position heap valid.allocations pair (by rw [partition]; exact List.mem_append_right _ member),
      (List.mem_zipIdx member).1, valid.canonical _ present, valid.copyBounds _ inside,
      valid.references.objects _ present, valid.ownerLayout _ present⟩
  have emptyCopies : AllocationSlots originals.length [] sourceTable.copies := by
    rw [unchangedCopies]
    exact AllocationSlots.empty _
  obtain ⟨tables, copiesChecked, indexedCopies, unchangedSources, unchangedUnits⟩ :=
    validateDestinationRecords_success copyPairs sourceTable [] emptyCopies copyRecords
      (by simpa [copyPairs, List.zipIdx_map_fst] using valid.copyIds)
  have copyIndex : AllocationSlots originals.length copies tables.copies := by
    simpa [copyPairs, List.zipIdx_map_fst] using indexedCopies
  have originIndex : AllocationSlots originals.length originals tables.origins := unchangedSources ▸ sourceIndex
  have unitLedger : IdLedger (originalUnitIds heap originals) tables.seen := by
    rw [unchangedUnits]
    simpa only [unitKeys] using sourceLedger
  have allocations : heap.objects.toList.zipIdx.foldlM (validateAllocation heap.objects.size originals.length)
      ⟨Array.replicate originals.length none, Array.replicate originals.length none, seen⟩ = .ok tables := by
    simp [partition, List.foldlM_append, sourcesChecked, copiesChecked, bind, Except.bind]
  have dense := checkDenseUnitIds_complete tables.seen _ unitLedger valid.units
  obtain ⟨remembered, owners, ownerLedger⟩ := checkRememberedOwners_success heap valid.remembered.owners valid.remembered.unique
  have strong := checkStrongOwners_success heap remembered ownerLedger valid.remembered.strong
  simp only [bind, Except.bind, pure, Except.pure] at strong
  unfold validateStructure
  simp only [count, roots, externals, allocations, dense, owners, strong, bind, Except.bind, pure, Except.pure]
  rw [Std.Legacy.Range.forIn_eq_forIn_range']
  apply Eq.trans (congrArg (fun result : Except String PUnit =>
    Except.bind result (fun _ => Except.ok ())) (a₂ := .ok PUnit.unit) ?_) rfl
  apply forIn_yields_success
  intro i member cursor
  cases cursor
  have bounded : i < originals.length := by simpa [Std.Legacy.Range.size, List.mem_range'] using member
  have sourceSlot : i < tables.origins.size := by simpa [originIndex.size] using bounded
  have copySlot : i < tables.copies.size := by simpa [copyIndex.size] using bounded
  have sourceLookup := (getElem!_pos tables.origins i sourceSlot).trans (originIndex.lookup i sourceSlot)
  have copyLookup := (getElem!_pos tables.copies i copySlot).trans (copyIndex.lookup i copySlot)
  cases found : originals.find? (fun a => logicalOrdinal a.objectId == i) with
  | none =>
      have missing := List.find?_eq_none.mp found
      have key := valid.originalIds.mem_iff.mpr (List.mem_range.mpr bounded)
      obtain ⟨a, inside, same⟩ := List.mem_map.mp key
      exact False.elim (missing a inside (by simpa using same))
  | some a =>
      have inside := List.mem_of_find?_eq_some found
      have same : logicalOrdinal a.objectId = i := by simpa using List.find?_some found
      have compatible : AllocationPairValid a (copies.find? (fun b => logicalOrdinal b.objectId == i)) := by
        simpa only [same] using valid.pairs a inside
      have accepted := checkObjectPair_complete a _ compatible
      simp [sourceLookup, copyLookup, found, accepted]

/-- Public validation succeeds as well; sorting is the identity on dense records. -/
theorem validateHeap_completed_complete (heap : HeapSnapshot) (originals copies : List Allocation)
    (valid : CompletedWellFormed heap originals copies) : validateHeap heap = .ok () := by
  have structural := validateStructure_completed_complete heap originals copies valid
  have ordered := orderedHeap_eq_of_dense heap valid.allocations
  simp [validateHeap, validateAndOrder, ordered, structural, valid.remembered.weak,
    bind, Except.bind, pure, Except.pure]

/-- Serialization has no remaining failure path for a completed well-formed heap. -/
theorem writeHeap_completed_complete (heap : HeapSnapshot) (originals copies : List Allocation)
    (valid : CompletedWellFormed heap originals copies) : ∃ bytes, writeHeap heap = .ok bytes := by
  have structural := validateStructure_completed_complete heap originals copies valid
  have ordered := orderedHeap_eq_of_dense heap valid.allocations
  have checked : validateAndOrder heap = .ok ⟨heap, valid.remembered.weak⟩ := by
    simp [validateAndOrder, ordered, structural, valid.remembered.weak, bind, Except.bind, pure, Except.pure]
  simp only [writeHeap, checked, bind, Except.bind, pure, Except.pure]
  exact ⟨_, rfl⟩

end CloverGC.Differential.Format
