import CloverGC.Differential.HeapPreparationProofs
import Init.Data.Vector.Zip

namespace CloverGC.Differential

/-- Each logical object index retains the exact record paired with it on input. -/
def OriginalsIndexed (initial : Format.HeapSnapshot)
    (objects : Vector (Fin initial.objects.size) initial.objects.size)
    (originals : Vector Format.Allocation initial.objects.size) : Prop :=
  ∀ pair ∈ (initial.objects.toVector.zip objects).toArray.toList, originals.get pair.2 = pair.1

theorem PhysicalObjectMap.values_perm (mapped : PhysicalObjectMap initial objects)
    (ids : Format.FreshIds initial) :
    (objects.toList.map Fin.val).Perm (List.range initial.objects.size) := by
  rw [mapped]
  exact ids.objects

theorem PhysicalObjectMap.unique (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial) :
    objects.toList.Nodup := by
  have values := (mapped.values_perm ids).symm.nodup List.nodup_range
  exact (List.pairwise_map.mp values).imp (fun different same => different (congrArg Fin.val same))

theorem PhysicalObjectMap.covers (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial)
    (object : Fin initial.objects.size) : object ∈ objects.toList := by
  have member := (mapped.values_perm ids).mem_iff.mpr (List.mem_range.mpr object.isLt)
  obtain ⟨value, inside, same⟩ := List.mem_map.mp member
  exact (Fin.ext same : value = object) ▸ inside

/-- The retained map is injective on physical allocation slots. -/
theorem PhysicalObjectMap.injective (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial)
    (left right : Fin initial.objects.size) (same : objects.get left = objects.get right) : left = right := by
  apply Fin.ext
  apply (mapped.unique ids).eq_of_getElem_eq (by simp) (by simp)
  change objects[left.val] = objects[right.val] at same
  simpa using same

/-- Every logical index is selected by some physical allocation slot. -/
theorem PhysicalObjectMap.surjective (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial)
    (object : Fin initial.objects.size) : ∃ physical, objects.get physical = object := by
  have member : object ∈ objects := by simpa using mapped.covers ids object
  obtain ⟨i, inside, same⟩ := Vector.getElem_of_mem member
  exact ⟨⟨i, inside⟩, same⟩

/-- Original indexing agrees with every position of the physical/logical pairing. -/
theorem OriginalsIndexed.at (indexed : OriginalsIndexed initial objects originals)
    (physical : Fin initial.objects.size) :
    originals.get (objects.get physical) = initial.objects.toVector.get physical := by
  let pairs := (initial.objects.toVector.zip objects).toArray.toList
  have within : physical.val < pairs.length := by simp [pairs]
  have member := List.getElem_mem (l := pairs) within
  have paired : (initial.objects.toVector.get physical, objects.get physical) ∈ pairs := by
    change (initial.objects.toVector[physical.val], objects[physical.val]) ∈ pairs
    simpa only [pairs, Vector.toList_toArray, Vector.getElem_toList, Vector.getElem_zip] using member
  exact indexed _ paired

/-- Every logical original has an actual input allocation as its source. -/
theorem OriginalsIndexed.source (indexed : OriginalsIndexed initial objects originals)
    (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial)
    (object : Fin initial.objects.size) :
    ∃ physical, objects.get physical = object ∧ originals.get object = initial.objects.toVector.get physical := by
  obtain ⟨physical, same⟩ := mapped.surjective ids object
  exact ⟨physical, same, same ▸ indexed.at physical⟩

/-- A converted reference selects the exact wire allocation at that physical ID. -/
theorem OriginalsIndexed.reference (indexed : OriginalsIndexed initial objects originals)
    (id : Format.AllocationId) (object : Fin initial.objects.size)
    (projected : ModelReference objects (some id) (some object)) :
    initial.objects[id.value]? = some (originals.get object) := by
  obtain ⟨physical, position, same⟩ := projected
  have stored := indexed.at physical
  rw [same] at stored
  rw [← position, Array.getElem?_eq_getElem physical.isLt]
  exact congrArg some stored.symm

/-- Wire allocation IDs coincide with positions, independently of logical identity. -/
theorem Format.FreshIds.allocation_at (ids : Format.FreshIds initial) (physical : Fin initial.objects.size) :
    (initial.objects.toVector.get physical).allocationId.value = physical.val := by
  change initial.objects[physical.val].allocationId.value = physical.val
  have within : physical.val < initial.objects.toList.length := by simp
  have same := congrArg (fun values : List Nat => values[physical.val]?) ids.allocations
  simpa [List.getElem?_map, List.getElem?_eq_getElem within,
    List.getElem?_range physical.isLt] using same

/-- Logical indexing permutes original records; it cannot drop or duplicate one. -/
theorem OriginalsIndexed.perm (indexed : OriginalsIndexed initial objects originals)
    (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial) :
    originals.toList.Perm initial.objects.toList := by
  have bounded : ∀ i ∈ objects.toList.map Fin.val, i < initial.objects.size := by
    intro i member
    obtain ⟨object, _, rfl⟩ := List.mem_map.mp member
    exact object.isLt
  have permuted := (mapped.values_perm ids).pmap
    (fun i (inside : i < initial.objects.size) => originals.get ⟨i, inside⟩)
    (H₁ := bounded) (H₂ := fun _ member => List.mem_range.mp member)
  have physical : (objects.toList.map Fin.val).pmap
      (fun i (inside : i < initial.objects.size) => originals.get ⟨i, inside⟩) bounded =
      initial.objects.toList := by
    apply List.ext_getElem
    · simp
    · intro i left right
      simpa [Vector.get] using indexed.at ⟨i, by simpa using right⟩
  have logical : (List.range initial.objects.size).pmap
      (fun i (inside : i < initial.objects.size) => originals.get ⟨i, inside⟩)
      (fun _ member => List.mem_range.mp member) =
      originals.toList := by
    apply List.ext_getElem
    · simp
    · intro i left right
      simp [Vector.get]
  rw [physical, logical] at permuted
  exact permuted.symm

/-- Converted references and indexed originals agree on the physical allocation ID. -/
theorem OriginalsIndexed.reference_id (indexed : OriginalsIndexed initial objects originals)
    (ids : Format.FreshIds initial) (allocation : Format.AllocationId) (object : Fin initial.objects.size)
    (projected : ModelReference objects (some allocation) (some object)) :
    (originals.get object).allocationId = allocation := by
  obtain ⟨physical, position, same⟩ := projected
  have stored := indexed.at physical
  rw [same] at stored
  have value : (originals.get object).allocationId.value = allocation.value := by
    rw [stored]
    exact (ids.allocation_at physical).trans position
  exact congrArg Format.AllocationId.mk value

end CloverGC.Differential
