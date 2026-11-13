import CloverGC.Differential.HeapGenerationalPreparation
import Init.Data.List.FinRange

namespace CloverGC.Differential

/-- The one global list of owner and external repair locations. -/
def weakLocationIds (input : Copying.Weak.Input n k) : List (Fin k) :=
  (List.finRange n).flatMap input.ownerUnits ++ input.external

/-- Every logical unit has exactly one repair location. -/
def WeakLocationLedger (input : Copying.Weak.Input n k) : Prop :=
  ((weakLocationIds input).map Fin.val).Perm (List.range k)

/-- The global location ledger supplies storage uniqueness and total coverage,
    independently of executable weak-heap validation. -/
theorem weakLocations_wellFormed (input : Copying.Weak.Input n k) (ledger : WeakLocationLedger input) :
    Copying.Weak.WellFormed input ∧
      ∀ id, id ∈ input.external ∨ ∃ owner, id ∈ input.ownerUnits owner := by
  have numbered := ledger.symm.nodup List.nodup_range
  have unique : (weakLocationIds input).Nodup :=
    (List.pairwise_map.mp numbered).imp (fun different same => different (congrArg Fin.val same))
  have parts := List.nodup_append.mp unique
  have rows := List.pairwise_flatMap.mp parts.1
  have locations : ∀ id, id ∈ weakLocationIds input := by
    intro id
    have member := ledger.mem_iff.mpr (List.mem_range.mpr id.isLt)
    obtain ⟨location, inside, same⟩ := List.mem_map.mp member
    exact (Fin.ext same : location = id) ▸ inside
  constructor
  · constructor
    · exact parts.2.1
    · intro owner
      exact rows.1 owner (List.mem_finRange owner)
    · intro left right different id inLeft inRight
      have indexDifferent : left.val ≠ right.val := fun same => different (Fin.ext same)
      by_cases less : left.val < right.val
      · have related := rows.2.rel_getElem_of_lt (i := left.val) (j := right.val) (by simp) (by simp) less
        have disjoint : ∀ a ∈ input.ownerUnits left, ∀ b ∈ input.ownerUnits right, a ≠ b := by simpa using related
        exact disjoint id inLeft id inRight rfl
      · have less : right.val < left.val := by omega
        have related := rows.2.rel_getElem_of_lt (i := right.val) (j := left.val) (by simp) (by simp) less
        have disjoint : ∀ a ∈ input.ownerUnits right, ∀ b ∈ input.ownerUnits left, a ≠ b := by simpa using related
        exact disjoint id inRight id inLeft rfl
    · intro owner id external owned
      exact parts.2.2 id (List.mem_flatMap.mpr ⟨owner, List.mem_finRange owner, owned⟩) id external rfl
  · intro id
    rcases List.mem_append.mp (locations id) with owned | external
    · obtain ⟨owner, _, inside⟩ := List.mem_flatMap.mp owned
      exact Or.inr ⟨owner, inside⟩
    · exact Or.inl external


/-- Converted owner/external lists inherit the wire format's one global ID domain. -/
theorem modelWeakLocations_ledger (initial : Format.HeapSnapshot) (valid : Format.FreshWellFormed initial)
    (objects : Vector (Fin initial.objects.size) initial.objects.size) (mapped : PhysicalObjectMap initial objects)
    (originals : Vector Format.Allocation initial.objects.size) (indexed : OriginalsIndexed initial objects originals)
    (input : Copying.Weak.Input initial.objects.size k) (count : k = (Format.freshUnitIds initial).length)
    (owners : ∀ owner, ModelUnitIds (originals.get owner).weak.toList (input.ownerUnits owner))
    (external : ModelUnitIds initial.externalWeak.toList input.external) : WeakLocationLedger input := by
  let ownerKeys := fun owner : Format.Allocation => owner.weak.toList.map (fun unit => unit.unitId.value)
  have rows : (List.finRange initial.objects.size).map originals.get = originals.toList := by
    rw [List.finRange, List.map_ofFn]
    rw [← Vector.toList_ofFn]
    congr 1
    exact Vector.ofFn_getElem
  have rowKeys : (List.finRange initial.objects.size).flatMap (fun owner => ownerKeys (originals.get owner)) =
      originals.toList.flatMap ownerKeys := by
    simpa only [List.flatMap_map, Function.comp_def] using congrArg (List.flatMap ownerKeys) rows
  have owned (owner) : (input.ownerUnits owner).map Fin.val = ownerKeys (originals.get owner) := owners owner
  have externalKeys : input.external.map Fin.val = initial.externalWeak.toList.map (fun unit => unit.unitId.value) := external
  have keys : (weakLocationIds input).map Fin.val =
      originals.toList.flatMap ownerKeys ++ initial.externalWeak.toList.map (fun unit => unit.unitId.value) := by
    simp only [weakLocationIds, List.map_append, List.map_flatMap, owned, rowKeys, externalKeys]
  have permuted := List.Perm.flatMap_right ownerKeys (indexed.perm mapped valid.identities)
  have originalKeys : (initial.objects.toList.flatMap ownerKeys ++ initial.externalWeak.toList.map (fun unit => unit.unitId.value)).Perm
      (Format.freshUnitIds initial) := by
    simpa only [Format.freshUnitIds, List.map_append, List.map_flatMap, ownerKeys] using
      (List.perm_append_comm (l₁ := initial.objects.toList.flatMap ownerKeys)
        (l₂ := initial.externalWeak.toList.map (fun unit => unit.unitId.value)))
  have complete := ((permuted.append_right _).trans originalKeys).trans valid.units
  unfold WeakLocationLedger
  rw [keys]
  simpa only [count] using complete

end CloverGC.Differential
