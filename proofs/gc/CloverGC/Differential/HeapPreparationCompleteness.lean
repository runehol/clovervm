import CloverGC.Differential.HeapWeakPreparation

namespace CloverGC.Differential

/-- Independent correspondence of the complete prepared model, including the
    first-class weak invariant needed by both collection modes. -/
structure PreparedHeapProjection (initial : Format.HeapSnapshot)
    (heap : PreparedHeap initial.objects.size k) : Prop where
  snapshot : heap.initial = initial
  objectIds : PhysicalObjectMap initial heap.objectOfAllocation
  originals : OriginalsIndexed initial heap.objectOfAllocation heap.originals
  unitCount : k = (Format.freshUnitIds initial).length
  roots : ModelStrongReferences heap.objectOfAllocation initial.roots.toList heap.roots
  children : ∀ owner, ModelStrongReferences heap.objectOfAllocation (heap.originals.get owner).strong.toList
    (heap.generational.data.graph.strongChildren owner)
  kinds : ∀ owner, ModelOriginalKind (heap.originals.get owner).state (heap.generational.data.objectKind owner)
  remembered : ModelRememberedOwners heap.objectOfAllocation initial.remembered.toList heap.generational.data.rememberedOwners
  ownerUnits : ∀ owner, ModelUnitIds (heap.originals.get owner).weak.toList (heap.weak.ownerUnits owner)
  externalUnits : ModelUnitIds initial.externalWeak.toList heap.weak.external
  templates : TemplatesIndexed heap.objectOfAllocation
    (initial.objects.toList.flatMap (fun owner => owner.weak.toList) ++ initial.externalWeak.toList) heap.weak.templates
  weakValid : Generational.Weak.WeakWellFormed ⟨heap.generational, heap.weak⟩

/-- Every independently well-formed fresh snapshot converts successfully, with
    exact correspondence of every field and no executable-success premise. -/
theorem prepareModel_complete (initial : Format.HeapSnapshot) (valid : Format.FreshWellFormed initial) :
    ∃ result, prepareModel initial = .ok result ∧ PreparedHeapProjection initial result.2.val := by
  obtain ⟨objects, objectCheck, mapped⟩ := modelObjectIds_succeeds initial valid.identities
  obtain ⟨originals, originalCheck, indexing⟩ := indexOriginals_succeeds initial valid.identities objects mapped
  have indexed : OriginalsIndexed initial objects originals := indexing
  let units := initial.objects.toList.flatMap (fun owner => owner.weak.toList) ++ initial.externalWeak.toList
  let k := units.length
  have count : k = (Format.freshUnitIds initial).length := by
    simp [k, units, Format.freshUnitIds, Nat.add_comm]
  have unitBound (unit : Format.WeakUnit) (member : unit ∈ units) : unit.unitId.value < k := by
    have stored : unit ∈ initial.externalWeak.toList ++ initial.objects.toList.flatMap (fun owner => owner.weak.toList) := by
      rcases List.mem_append.mp member with owned | external
      · exact List.mem_append_right _ owned
      · exact List.mem_append_left _ external
    have present : unit.unitId.value ∈ Format.freshUnitIds initial := List.mem_map.mpr ⟨unit, stored, rfl⟩
    simpa only [count] using List.mem_range.mp (valid.units.mem_iff.mp present)
  have inputMember (a : Format.Allocation) (member : a ∈ originals.toList) : a ∈ initial.objects.toList :=
    (indexed.perm mapped valid.identities).mem_iff.mp member
  obtain ⟨templates, templateCheck, templateProjection⟩ := indexHeapTemplates_succeeds initial valid objects
  obtain ⟨children, childrenCheck, fields⟩ := modelChildren_succeeds objects originals
    (fun a member => (valid.references.objects a (inputMember a member)).1)
  obtain ⟨kinds, kindsCheck, placements⟩ := modelKinds_succeeds originals
    (fun a member => valid.states a (inputMember a member))
  obtain ⟨remembered, rememberedCheck, owners⟩ := modelRememberedOwners_succeeds initial objects valid.remembered.owners
  let raw : Generational.RawGenerationalHeap initial.objects.size := ⟨⟨children.get⟩, kinds.get, remembered⟩
  have strongValid : Generational.WellFormed raw :=
    modelGenerational_wellFormed initial valid objects mapped originals indexed children.get kinds.get remembered fields placements owners
  let strong : Generational.GenerationalHeap initial.objects.size := ⟨raw, strongValid⟩
  have strongCheck : raw.validate = .ok strong := validateGenerational_complete raw strongValid
  obtain ⟨ownerUnits, ownerCheck, ownerProjection⟩ := modelOwnerUnits_succeeds k originals (by
    intro a member unit owned
    exact unitBound unit (List.mem_append_left _ (List.mem_flatMap.mpr ⟨a, inputMember a member, owned⟩)))
  obtain ⟨external, externalCheck, externalProjection⟩ := modelUnitIds_succeeds k initial.externalWeak.toList
    (fun unit member => unitBound unit (List.mem_append_right _ member))
  obtain ⟨roots, rootsCheck, rootProjection⟩ := strongReferences_succeeds objects initial.roots valid.references.roots
  let weak : Copying.Weak.Input initial.objects.size k := ⟨templates.get, ownerUnits.get, external⟩
  have locations : WeakLocationLedger weak := modelWeakLocations_ledger initial valid objects mapped originals indexed
    weak count ownerProjection externalProjection
  have weakValid : Generational.Weak.WeakWellFormed ⟨strong, weak⟩ := modelWeak_wellFormed initial valid objects mapped
    originals indexed ⟨strong, weak⟩ locations ownerProjection templateProjection placements owners
  let heap : PreparedHeap initial.objects.size k := ⟨initial, originals, objects, strong, weak, roots⟩
  let result : Σ k, { heap : PreparedHeap initial.objects.size k // heap.initial = initial } := ⟨k, ⟨heap, rfl⟩⟩
  refine ⟨result, ?_, ?_⟩
  · dsimp only [raw] at strongCheck
    dsimp only [k, units] at ownerCheck externalCheck
    simp only [prepareModel, objectCheck, originalCheck, templateCheck, childrenCheck, kindsCheck,
      rememberedCheck, strongCheck, ownerCheck, externalCheck, rootsCheck, bind, Except.bind, pure, Except.pure]
    rfl
  · change PreparedHeapProjection initial heap
    exact ⟨rfl, mapped, indexed, count, rootProjection, fields, placements, owners,
      ownerProjection, externalProjection, templateProjection, weakValid⟩

/-- Every valid fresh input prepares successfully with its exact ordered snapshot. -/
theorem prepareHeap_complete (input : Format.HeapSnapshot)
    (valid : Format.FreshWellFormed (Format.orderedHeap input)) :
    ∃ k, ∃ heap : PreparedHeap (Format.orderedHeap input).objects.size k,
      prepareHeap input = .ok ⟨(Format.orderedHeap input).objects.size, k, heap⟩ ∧
      PreparedHeapProjection (Format.orderedHeap input) heap := by
  obtain ⟨result, checked, projected⟩ := prepareModel_complete (Format.orderedHeap input) valid
  obtain ⟨k, heap⟩ := result
  refine ⟨k, heap.val, ?_, projected⟩
  simp only [prepareHeap, Format.prepareFreshHeap_complete input valid, checked,
    bind, Except.bind, pure, Except.pure]

end CloverGC.Differential
