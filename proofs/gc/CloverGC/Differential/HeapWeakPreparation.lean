import CloverGC.Differential.HeapWeakStoragePreparation
import CloverGC.Differential.HeapWeakTemplateTargets

namespace CloverGC.Differential

private theorem ModelTemplate.target_source {objects : Vector (Fin n) n} {before : Format.WeakContents}
    {after : Copying.Weak.Template n} (projected : ModelTemplate objects before after)
    (target : Fin n) (member : some target ∈ after.targets) :
    ∃ allocation ∈ before.targets, ModelReference objects (some allocation) (some target) := by
  have included : target ∈ after.targets.filterMap id := List.mem_filterMap.mpr ⟨some target, member, rfl⟩
  exact ListProjection.after_exists (relation := fun allocation (object : Fin n) =>
    ModelReference objects (some allocation) (some object)) (b := target) projected.targets included

/-- All weak-generation constraints follow from wire validity and conversion
    correspondence, including stored targets in inactive entries. -/
theorem modelWeak_wellFormed (initial : Format.HeapSnapshot) (valid : Format.FreshWellFormed initial)
    (objects : Vector (Fin initial.objects.size) initial.objects.size) (mapped : PhysicalObjectMap initial objects)
    (originals : Vector Format.Allocation initial.objects.size) (indexed : OriginalsIndexed initial objects originals)
    (heap : Generational.Weak.RawWeakHeap initial.objects.size k) (locations : WeakLocationLedger heap.units)
    (ownerLists : ∀ owner, ModelUnitIds (originals.get owner).weak.toList (heap.units.ownerUnits owner))
    (templates : TemplatesIndexed objects
      (initial.objects.toList.flatMap (fun owner => owner.weak.toList) ++ initial.externalWeak.toList) heap.units.templates)
    (kinds : ∀ owner, ModelOriginalKind (originals.get owner).state (heap.strong.data.objectKind owner))
    (remembered : ModelRememberedOwners objects initial.remembered.toList heap.strong.data.rememberedOwners) :
    Generational.Weak.WeakWellFormed heap := by
  obtain ⟨storage, located⟩ := weakLocations_wellFormed heap.units locations
  refine ⟨storage, located, ?_⟩
  intro owner id target old owned member young
  have idsEq : (heap.units.ownerUnits owner).map Fin.val =
      (originals.get owner).weak.toList.map (fun unit => unit.unitId.value) := ownerLists owner
  have idMember : id.val ∈ (heap.units.ownerUnits owner).map Fin.val := List.mem_map.mpr ⟨id, owned, rfl⟩
  rw [idsEq] at idMember
  obtain ⟨unit, unitMember, unitId⟩ := List.mem_map.mp idMember
  have ownerInside := indexed.member mapped valid.identities owner
  have unitInside : unit ∈ initial.objects.toList.flatMap (fun owner => owner.weak.toList) ++ initial.externalWeak.toList :=
    List.mem_append_left _ (List.mem_flatMap.mpr ⟨originals.get owner, ownerInside, unitMember⟩)
  obtain ⟨slot, slotId, projected⟩ := templates unit unitInside
  have sameSlot : slot = id := Fin.ext (slotId.trans unitId)
  subst slot
  obtain ⟨allocation, storedTarget, related⟩ := projected.target_source target member
  have targetInside := indexed.member mapped valid.identities target
  have oldWire : (originals.get owner).state.isYoung = false := (kinds owner).young.symm.trans old
  have youngWire : (originals.get target).state.isYoung = true := (kinds target).young.symm.trans young
  have ownerRemembered := valid.remembered.weak (originals.get owner) ownerInside oldWire unit unitMember
    allocation storedTarget (originals.get target) targetInside (indexed.reference_id valid.identities allocation target related) youngWire
  obtain ⟨physical, selected, stored⟩ := indexed.source mapped valid.identities owner
  have projectOwner : ModelReference objects (some (originals.get owner).allocationId) (some owner) := by
    refine ⟨physical, ?_, selected⟩
    rw [stored]
    exact (valid.identities.allocation_at physical).symm
  obtain ⟨converted, inside, convertedProjection⟩ := ListProjection.before_exists remembered ownerRemembered
  have same : converted = owner := ModelReference.unique convertedProjection projectOwner
  exact same ▸ inside

/-- A well-formed weak model is accepted by its actual first-class validator. -/
theorem validateWeak_complete (heap : Generational.Weak.RawWeakHeap n k)
    (valid : Generational.Weak.WeakWellFormed heap) :
    heap.validate = .ok ⟨heap, valid⟩ := by
  simp [Generational.Weak.RawWeakHeap.validate, valid, pure, Except.pure]

end CloverGC.Differential
