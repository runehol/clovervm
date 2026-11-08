import CloverGC.GenerationalHeap
import CloverGC.WeakRepair

namespace CloverGC.Generational.Weak

structure RawWeakHeap (n k : Nat) where
  strong : GenerationalHeap n
  units : Copying.Weak.Input n k
  rememberedWeak : List (Fin k)

/-- Unit IDs denote disjoint repair storage. Coverage includes all stored
    targets, even when a null mandatory guard makes an entry inactive. -/
structure WeakWellFormed (heap : RawWeakHeap n k) : Prop where
  storage : Copying.Weak.WellFormed heap.units
  located : ∀ id, id ∈ heap.units.external ∨ ∃ owner, id ∈ heap.units.ownerUnits owner
  remembered_unique : heap.rememberedWeak.Nodup
  remembered_old : ∀ id ∈ heap.rememberedWeak,
    ∃ owner, heap.strong.data.isOld owner ∧ id ∈ heap.units.ownerUnits owner
  old_to_young_covered : ∀ owner id target, heap.strong.data.isOld owner →
    id ∈ heap.units.ownerUnits owner → some target ∈ (heap.units.templates id).targets →
    heap.strong.data.isYoung target = true → id ∈ heap.rememberedWeak

set_option synthInstance.maxSize 512 in
private def storageDecidable (units : Copying.Weak.Input n k) :
    Decidable (Copying.Weak.WellFormed units) :=
  decidable_of_iff
    (units.external.Nodup ∧ (∀ owner, (units.ownerUnits owner).Nodup) ∧
      (∀ left right, left ≠ right → ∀ id ∈ units.ownerUnits left, id ∉ units.ownerUnits right) ∧
      (∀ owner, ∀ id ∈ units.external, id ∉ units.ownerUnits owner))
    ⟨fun ⟨a, b, c, d⟩ => ⟨a, b, c, d⟩, fun h => ⟨h.1, h.2, h.3, h.4⟩⟩

set_option synthInstance.maxSize 512 in
instance (heap : RawWeakHeap n k) : Decidable (WeakWellFormed heap) := by
  letI := storageDecidable heap.units
  exact decidable_of_iff
    (Copying.Weak.WellFormed heap.units ∧
      (∀ id, id ∈ heap.units.external ∨ ∃ owner, id ∈ heap.units.ownerUnits owner) ∧
      heap.rememberedWeak.Nodup ∧
      (∀ id ∈ heap.rememberedWeak,
        ∃ owner, heap.strong.data.isOld owner ∧ id ∈ heap.units.ownerUnits owner) ∧
      (∀ owner id target, heap.strong.data.isOld owner → id ∈ heap.units.ownerUnits owner →
        some target ∈ (heap.units.templates id).targets → heap.strong.data.isYoung target = true →
        id ∈ heap.rememberedWeak))
    ⟨fun ⟨a, b, c, d, e⟩ => ⟨a, b, c, d, e⟩, fun h => ⟨h.1, h.2, h.3, h.4, h.5⟩⟩

structure WeakHeap (n k : Nat) where
  data : RawWeakHeap n k
  valid : WeakWellFormed data

def RawWeakHeap.validate (heap : RawWeakHeap n k) : Except String (WeakHeap n k) :=
  if valid : WeakWellFormed heap then pure ⟨heap, valid⟩
  else throw "weak heap: units must have unique locations and remembered old units must cover every young target"

theorem WeakHeap.owner_unique (heap : WeakHeap n k) (id : Fin k) (left right : Fin n)
    (inLeft : id ∈ heap.data.units.ownerUnits left) (inRight : id ∈ heap.data.units.ownerUnits right) :
    left = right := by
  by_cases same : left = right
  · exact same
  · exact False.elim (heap.valid.storage.owners_disjoint left right same id inLeft inRight)

theorem WeakHeap.owner_not_external (heap : WeakHeap n k) (owner : Fin n) (id : Fin k)
    (owned : id ∈ heap.data.units.ownerUnits owner) : id ∉ heap.data.units.external := by
  intro external
  exact heap.valid.storage.external_disjoint owner id external owned

theorem WeakHeap.remembered_not_external (heap : WeakHeap n k) (id : Fin k)
    (remembered : id ∈ heap.data.rememberedWeak) : id ∉ heap.data.units.external := by
  obtain ⟨owner, _, owned⟩ := heap.valid.remembered_old id remembered
  exact heap.owner_not_external owner id owned

theorem WeakHeap.remembered_owner_old (heap : WeakHeap n k) (owner : Fin n) (id : Fin k)
    (remembered : id ∈ heap.data.rememberedWeak) (owned : id ∈ heap.data.units.ownerUnits owner) :
    heap.data.strong.data.isOld owner := by
  obtain ⟨other, old, member⟩ := heap.valid.remembered_old id remembered
  exact heap.owner_unique id other owner member owned ▸ old

theorem WeakHeap.unremembered_targets_old (heap : WeakHeap n k) (owner : Fin n) (id : Fin k)
    (old : heap.data.strong.data.isOld owner) (owned : id ∈ heap.data.units.ownerUnits owner)
    (unremembered : id ∉ heap.data.rememberedWeak) (target : Fin n)
    (member : some target ∈ (heap.data.units.templates id).targets) :
    heap.data.strong.data.isOld target := by
  cases young : heap.data.strong.data.isYoung target with
  | false => exact young
  | true => exact False.elim (unremembered (heap.valid.old_to_young_covered owner id target old owned member young))

end CloverGC.Generational.Weak
