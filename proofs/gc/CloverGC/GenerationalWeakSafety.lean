import CloverGC.GenerationalWeakCorrectness

namespace CloverGC.Generational.Weak

open Copying

theorem repair_template_safe (input : CollectionInput n) (copying : CopyState n)
    (template : Copying.Weak.Template n) (h : CopyInvariant input.copyInput copying)
    (reference : Reference n)
    (member : some reference ∈ (Copying.Weak.repairUnit (resolve input copying .late) template.initial).1.targets) :
    Safe input copying reference := by
  obtain ⟨field, inFields, eq⟩ := Copying.Weak.repairUnit_target _ _ reference member
  rw [Copying.Weak.Template.initial, Copying.Weak.targets_map] at inFields
  obtain ⟨original, _, rfl⟩ := List.mem_map.mp inFields
  cases original with
  | none => simp [Copying.Weak.repairField] at eq
  | some object =>
      cases resolved : resolve input copying .late (.source object) with
      | dead => simp [Copying.Weak.repairField, resolved] at eq
      | pending => exact False.elim (resolve_late_not_pending input copying (.source object) resolved)
      | resolved target =>
          have same : target = reference := by simpa [Copying.Weak.repairField, resolved] using eq
          subst reference
          exact resolved_source_safe input copying object target h resolved

theorem collection_registered_safe (input : Input n k) (id : Fin k)
    (registered : id ∈ (collectWeak input).1.registered) (reference : Reference n)
    (member : some reference ∈ ((collectWeak input).1.units id).targets) :
    Safe input.strongInput (collectWeak input).1.strong.copying reference := by
  rw [collection_units_correct input id registered] at member
  have strong : (collectWeak input).1.strong = (Generational.evacuate input.strongInput).1 :=
    congrArg Prod.fst (collectWeak_strong input)
  rw [strong]
  exact repair_template_safe input.strongInput _ (input.heap.data.units.templates id)
    (Generational.evacuate_invariant input.strongInput).tracking.copying reference member

/-- Unregistered units of retained owners also remain safe: live scanned
    owners register their units, so the only remaining case is unselected old
    storage, whose coverage witness excludes every young target. -/
theorem collection_retained_owner_safe (input : Input n k) (owner : Fin n)
    (retained : Retained input.strongInput owner) (id : Fin k)
    (owned : id ∈ input.heap.data.units.ownerUnits owner) (reference : Reference n)
    (member : some reference ∈ ((collectWeak input).1.units id).targets) :
    Safe input.strongInput (collectWeak input).1.strong.copying reference := by
  by_cases registered : id ∈ (collectWeak input).1.registered
  · exact collection_registered_safe input id registered reference member
  · have outside : input.strongInput.selected owner = false := by
      rcases retained with outside | live
      · exact outside
      · exact False.elim (registered ((collection_registered_iff input id).mpr (Or.inr (Or.inr ⟨owner, live, owned⟩))))
    cases mode : input.collection with
    | full => simp [Input.strongInput, CollectionInput.selected, mode] at outside
    | young =>
        have unremembered : owner ∉ input.heap.data.strong.data.rememberedOwners := by
          intro remembered
          apply registered
          apply (collection_registered_iff input id).mpr
          exact Or.inr (Or.inl (List.mem_flatMap.mpr ⟨owner,
            by simpa [Input.strongInput, Generational.CollectionInput.remembered, mode] using remembered, owned⟩))
        rw [collection_unregistered_unchanged input id registered,
          Copying.Weak.Template.initial, Copying.Weak.targets_map] at member
        obtain ⟨original, targetMember, eq⟩ := List.mem_map.mp member
        cases original with
        | none => simp at eq
        | some target =>
            have same : Reference.source target = reference := by simpa using eq
            subst reference
            have old := input.heap.unremembered_targets_old owner id
              (by simpa [Input.strongInput, CollectionInput.selected, mode] using outside)
              owned unremembered target targetMember
            exact Or.inl (by simpa [Input.strongInput, CollectionInput.selected, mode] using old)

/-- Safety covers external storage and units of retained owners. Units in
    discarded owners' diagnostic source storage are deliberately excluded. -/
theorem collection_disposal_safe (input : Input n k) :
    (∀ id ∈ input.heap.data.units.external, ∀ reference,
      some reference ∈ ((collectWeak input).1.units id).targets →
        Safe input.strongInput (collectWeak input).1.strong.copying reference) ∧
    (∀ owner, Retained input.strongInput owner → ∀ id ∈ input.heap.data.units.ownerUnits owner,
      ∀ reference, some reference ∈ ((collectWeak input).1.units id).targets →
        Safe input.strongInput (collectWeak input).1.strong.copying reference) := by
  constructor
  · intro id external reference member
    exact collection_registered_safe input id ((collection_registered_iff input id).mpr (Or.inl external)) reference member
  · intro owner retained id owned reference member
    exact collection_retained_owner_safe input owner retained id owned reference member

end CloverGC.Generational.Weak
