import CloverGC.Differential.HeapObjectMapping

namespace CloverGC.Differential

private theorem ListProjection.nodup (projected : ListProjection relation before after) (distinct : before.Nodup)
    (injective : ∀ {a b value}, relation a value → relation b value → a = b) : after.Nodup := by
  induction projected with
  | nil => simp
  | cons head tail ih =>
      obtain ⟨absent, distinct⟩ := List.nodup_cons.mp distinct
      refine List.nodup_cons.mpr ⟨?_, ih distinct⟩
      intro member
      obtain ⟨source, inside, related⟩ := tail.after_exists member
      exact absent ((injective head related).symm ▸ inside)

theorem ModelReference.unique {objects : Vector (Fin n) n} {id : Format.AllocationId} {left right : Fin n}
    (first : ModelReference objects (some id) (some left))
    (second : ModelReference objects (some id) (some right)) : left = right := by
  obtain ⟨one, positionOne, valueOne⟩ := first
  obtain ⟨two, positionTwo, valueTwo⟩ := second
  have same : one = two := Fin.ext (positionOne.trans positionTwo.symm)
  subst two
  exact valueOne.symm.trans valueTwo

private theorem ModelReference.source_unique (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial)
    (first : ModelReference objects (some left) (some object))
    (second : ModelReference objects (some right) (some object)) : left = right := by
  obtain ⟨one, positionOne, valueOne⟩ := first
  obtain ⟨two, positionTwo, valueTwo⟩ := second
  have same := mapped.injective ids one two (valueOne.trans valueTwo.symm)
  have value : left.value = right.value := positionOne.symm.trans ((congrArg Fin.val same).trans positionTwo)
  rcases left with ⟨left⟩
  rcases right with ⟨right⟩
  exact congrArg Format.AllocationId.mk value

private theorem ModelStrongReferences.source (projected : ModelStrongReferences objects cells output)
    (member : object ∈ output) :
    ∃ id, .reference (some id) ∈ cells ∧ ModelReference objects (some id) (some object) := by
  induction projected with
  | nil => cases member
  | immediate tail ih | null tail ih =>
      obtain ⟨id, inside, related⟩ := ih member
      exact ⟨id, by simp [inside], related⟩
  | reference head tail ih =>
      rcases List.mem_cons.mp member with same | inside
      · subst object; exact ⟨_, by simp, head⟩
      · obtain ⟨id, inside, related⟩ := ih inside
        exact ⟨id, by simp [inside], related⟩

theorem ModelOriginalKind.young (projected : ModelOriginalKind before after) :
    after.isYoung = before.isYoung := by
  cases before with
  | youngOriginal policy header | oldOriginal policy header =>
      cases policy <;> cases after <;>
        simp_all [ModelOriginalKind, Format.ObjectState.isYoung, Generational.OriginalKind.isYoung]
  | nonmovingOriginal marked | ordinaryDestination | nonmovingDestination =>
      cases after <;> simp_all [ModelOriginalKind, Format.ObjectState.isYoung, Generational.OriginalKind.isYoung]

private theorem Format.OldOriginal.not_young (old : Format.OldOriginal state) : state.isYoung = false := by
  cases state <;> simp_all [Format.OldOriginal, Format.ObjectState.isYoung]

theorem OriginalsIndexed.member (indexed : OriginalsIndexed initial objects originals)
    (mapped : PhysicalObjectMap initial objects) (ids : Format.FreshIds initial)
    (object : Fin initial.objects.size) : originals.get object ∈ initial.objects.toList := by
  obtain ⟨physical, _, stored⟩ := indexed.source mapped ids object
  rw [stored]
  change initial.objects.toList[physical.val] ∈ initial.objects.toList
  exact List.getElem_mem (by simp)

/-- The model's first-class generational invariant follows from wire validity and
    independent correspondence of all converted graph, kind and owner fields. -/
theorem modelGenerational_wellFormed (initial : Format.HeapSnapshot) (valid : Format.FreshWellFormed initial)
    (objects : Vector (Fin initial.objects.size) initial.objects.size) (mapped : PhysicalObjectMap initial objects)
    (originals : Vector Format.Allocation initial.objects.size) (indexed : OriginalsIndexed initial objects originals)
    (children : Fin initial.objects.size → List (Fin initial.objects.size))
    (kinds : Fin initial.objects.size → Generational.OriginalKind) (remembered : List (Fin initial.objects.size))
    (fields : ∀ owner, ModelStrongReferences objects (originals.get owner).strong.toList (children owner))
    (placements : ∀ owner, ModelOriginalKind (originals.get owner).state (kinds owner))
    (owners : ModelRememberedOwners objects initial.remembered.toList remembered) :
    Generational.WellFormed ⟨⟨children⟩, kinds, remembered⟩ := by
  constructor
  · exact ListProjection.nodup owners valid.remembered.unique
      (fun first second => ModelReference.source_unique mapped valid.identities first second)
  · intro owner member
    obtain ⟨id, inside, related⟩ := ListProjection.after_exists owners member
    obtain ⟨wireOwner, lookup, old⟩ := valid.remembered.owners id inside
    have selected := indexed.reference id owner related
    have same : wireOwner = originals.get owner := Option.some.inj (lookup.symm.trans selected)
    change (kinds owner).isYoung = false
    rw [(placements owner).young, ← same]
    exact old.not_young
  · intro owner child old inside young
    obtain ⟨id, cellMember, related⟩ := (fields owner).source inside
    have ownerInside := indexed.member mapped valid.identities owner
    have ownerOld : (originals.get owner).state.isYoung = false := (placements owner).young.symm.trans old
    have childYoung : (originals.get child).state.isYoung = true := (placements child).young.symm.trans young
    have wireRemembered := valid.remembered.strong (originals.get owner) ownerInside ownerOld id cellMember
      (originals.get child) (indexed.reference id child related) childYoung
    obtain ⟨ownerId, ownerMember, position⟩ := List.mem_map.mp wireRemembered
    obtain ⟨physical, selected, stored⟩ := indexed.source mapped valid.identities owner
    have physicalId : (originals.get owner).allocationId.value = physical.val := by
      rw [stored]
      exact valid.identities.allocation_at physical
    have projection : ModelReference objects (some ownerId) (some owner) :=
      ⟨physical, physicalId.symm.trans position.symm, selected⟩
    obtain ⟨converted, convertedMember, convertedProjection⟩ := ListProjection.before_exists owners ownerMember
    have same : converted = owner := ModelReference.unique convertedProjection projection
    exact same ▸ convertedMember

/-- First-class well-formedness is sufficient for actual model validation. -/
theorem validateGenerational_complete (heap : Generational.RawGenerationalHeap n)
    (valid : Generational.WellFormed heap) :
    heap.validate = .ok ⟨heap, valid⟩ := by
  simp [Generational.RawGenerationalHeap.validate, valid, pure, Except.pure]

end CloverGC.Differential
