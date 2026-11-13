import Init.Data.Vector.Monadic
import CloverGC.Differential.HeapRunnerProofs
import CloverGC.Differential.HeapFormatCompleteness

namespace CloverGC.Differential

/-- Wire references select logical objects through the physical-to-logical pairing. -/
def ModelReference (objects : Vector (Fin n) n) : Format.Reference → Option (Fin n) → Prop
  | none, none => True
  | some id, some object => ∃ index : Fin n, index.val = id.value ∧ objects.get index = object
  | _, _ => False

/-- Cell tags and opaque Int64 values survive conversion unchanged. -/
def ModelCell (objects : Vector (Fin n) n) : Format.Cell → Copying.Weak.Cell (Fin n) → Prop
  | .immediate before, .immediate after => before = after
  | .reference before, .reference after => ModelReference objects before after
  | _, _ => False

/-- Both guards and the payload are converted even for inactive entries. -/
def ModelTemplate (objects : Vector (Fin n) n) : Format.WeakContents → Copying.Weak.Template n → Prop
  | .field before, .field after => ModelCell objects before after
  | .entry before, .entry after => ModelReference objects before.firstGuard after.firstGuard ∧
      ModelReference objects before.secondGuard after.secondGuard ∧ ModelCell objects before.payload after.payload
  | _, _ => False

private theorem bind_ok (action : Except String α) (f : α → Except String β) (value : β) :
    (action >>= f) = .ok value ↔ ∃ a, action = .ok a ∧ f a = .ok value := by
  cases action <;> simp [bind, Except.bind]

private theorem map_ok (action : Except String α) (f : α → β) (value : β) :
    f <$> action = .ok value ↔ ∃ a, action = .ok a ∧ f a = value := by
  cases action <;> simp [Functor.map, Except.map]

theorem originalReference_projects (objects : Vector (Fin n) n) (id : Format.AllocationId) (object : Fin n) :
    originalReference objects id = .ok object ↔
      ∃ index : Fin n, index.val = id.value ∧ objects.get index = object := by
  constructor
  · intro accepted
    unfold originalReference at accepted
    cases checked : checkedFin id.value n with
    | error message => simp [checked, bind, Except.bind] at accepted
    | ok index =>
        simp [checked, bind, Except.bind, pure, Except.pure] at accepted
        exact ⟨index, (checkedFin_correct _ _ _).mp checked, accepted⟩
  · rintro ⟨index, physical, result⟩
    have checked := (checkedFin_correct _ _ index).mpr physical
    simp [originalReference, checked, result, bind, Except.bind, pure, Except.pure]

theorem modelReference_correct (objects : Vector (Fin n) n) (before : Format.Reference) (after : Option (Fin n)) :
    modelReference objects before = .ok after ↔ ModelReference objects before after := by
  cases before <;> cases after <;>
    simp [modelReference, ModelReference, Option.mapM, map_ok, originalReference_projects, pure, Except.pure]

theorem modelCell_correct (objects : Vector (Fin n) n) (before : Format.Cell) (after : Copying.Weak.Cell (Fin n)) :
    modelCell objects before = .ok after ↔ ModelCell objects before after := by
  cases before <;> cases after <;>
    simp [modelCell, ModelCell, modelReference_correct, bind_ok, pure, Except.pure]

theorem modelUnit_correct (objects : Vector (Fin n) n) (before : Format.WeakContents) (after : Copying.Weak.Template n) :
    modelUnit objects before = .ok after ↔ ModelTemplate objects before after := by
  cases before with
  | field value =>
      cases after <;> simp [modelUnit, ModelTemplate, bind_ok, modelCell_correct, pure, Except.pure]
  | entry entry =>
      cases after with
      | field value => simp [modelUnit, ModelTemplate, bind_ok, pure, Except.pure]
      | entry value =>
          cases value
          simp [modelUnit, ModelTemplate, bind_ok, modelReference_correct, modelCell_correct, pure, Except.pure]
          grind

/-- Every reference-valid wire reference converts successfully. -/
theorem modelReference_succeeds (objects : Vector (Fin n) n) (before : Format.Reference)
    (valid : Format.ReferenceValid n before) :
    ∃ after, modelReference objects before = .ok after ∧ ModelReference objects before after := by
  cases before with
  | none => exact ⟨none, (modelReference_correct _ _ _).mpr trivial, trivial⟩
  | some id =>
      let index : Fin n := ⟨id.value, valid id (by simp)⟩
      have projected : ModelReference objects (some id) (some (objects.get index)) := ⟨index, rfl, rfl⟩
      exact ⟨_, (modelReference_correct _ _ _).mpr projected, projected⟩

/-- Every reference-valid cell converts, with no condition on its Int64 payload. -/
theorem modelCell_succeeds (objects : Vector (Fin n) n) (before : Format.Cell)
    (valid : Format.CellReferencesValid n before) :
    ∃ after, modelCell objects before = .ok after ∧ ModelCell objects before after := by
  cases before with
  | immediate value => exact ⟨.immediate value, rfl, rfl⟩
  | reference ref =>
      obtain ⟨after, _, projected⟩ := modelReference_succeeds objects ref valid
      exact ⟨.reference after, (modelCell_correct _ _ _).mpr projected, projected⟩

/-- Every valid unit converts all its stored cells, regardless of activation. -/
theorem modelUnit_succeeds (objects : Vector (Fin n) n) (before : Format.WeakContents)
    (valid : Format.WeakContentsReferencesValid n before) :
    ∃ after, modelUnit objects before = .ok after ∧ ModelTemplate objects before after := by
  cases before with
  | field value =>
      obtain ⟨after, _, projected⟩ := modelCell_succeeds objects value valid
      exact ⟨.field after, (modelUnit_correct _ _ _).mpr projected, projected⟩
  | entry entry =>
      obtain ⟨first, _, firstProjects⟩ := modelReference_succeeds objects entry.firstGuard valid.1
      obtain ⟨second, _, secondProjects⟩ := modelReference_succeeds objects entry.secondGuard valid.2.1
      obtain ⟨payload, _, payloadProjects⟩ := modelCell_succeeds objects entry.payload valid.2.2
      have projected : ModelTemplate objects (.entry entry) (.entry ⟨first, second, payload⟩) :=
        ⟨firstProjects, secondProjects, payloadProjects⟩
      exact ⟨_, (modelUnit_correct _ _ _).mpr projected, projected⟩


/-- Strong conversion keeps non-null references in their original slot order. -/
inductive ModelStrongReferences (objects : Vector (Fin n) n) : List Format.Cell → List (Fin n) → Prop
  | nil : ModelStrongReferences objects [] []
  | immediate (tail : ModelStrongReferences objects rest output) :
      ModelStrongReferences objects (.immediate value :: rest) output
  | null (tail : ModelStrongReferences objects rest output) :
      ModelStrongReferences objects (.reference none :: rest) output
  | reference {id : Format.AllocationId} {object : Fin n}
      (head : ModelReference objects (some id) (some object))
      (tail : ModelStrongReferences objects rest output) :
      ModelStrongReferences objects (.reference (some id) :: rest) (object :: output)

/-- The executable scan is exactly the ordered strong-reference projection. -/
theorem strongReferences_correct (objects : Vector (Fin n) n) (cells : Array Format.Cell) (output : List (Fin n)) :
    strongReferences objects cells = .ok output ↔ ModelStrongReferences objects cells.toList output := by
  let convert : Format.Cell → Except String (Option (Fin n))
    | .reference (some id) => some <$> originalReference objects id
    | _ => pure none
  change cells.toList.filterMapM convert = .ok output ↔ _
  generalize cells.toList = values
  induction values generalizing output with
  | nil =>
      constructor
      · intro checked
        have same : output = [] := by simpa [pure, Except.pure] using checked.symm
        subst output
        exact .nil
      · intro projected; cases projected; rfl
  | cons cell rest ih =>
      cases cell with
      | immediate value =>
          simp only [List.filterMapM_cons, convert, bind, Except.bind, pure, Except.pure]
          exact (ih output).trans ⟨.immediate, fun projected => by cases projected; assumption⟩
      | reference ref =>
          cases ref with
          | none =>
              simp only [List.filterMapM_cons, convert, bind, Except.bind, pure, Except.pure]
              exact (ih output).trans ⟨.null, fun projected => by cases projected; assumption⟩
          | some id =>
              cases converted : originalReference objects id with
              | error message =>
                  simp only [List.filterMapM_cons, convert, converted, Functor.map, Except.map, bind, Except.bind,
                    reduceCtorEq, false_iff]
                  intro projected
                  cases projected with
                  | reference head _ =>
                      have success := (originalReference_projects objects id _).mpr head
                      simp [converted] at success
              | ok object =>
                  simp only [List.filterMapM_cons, convert, converted, Functor.map, Except.map, bind, Except.bind]
                  change (rest.filterMapM convert >>= fun tail => pure (object :: tail)) = .ok output ↔ _
                  rw [bind_ok]
                  constructor
                  · rintro ⟨tail, checked, same⟩
                    have same : object :: tail = output := by simpa [pure, Except.pure] using same
                    subst output
                    exact .reference ((originalReference_projects objects id object).mp converted) ((ih tail).mp checked)
                  · intro projected
                    cases projected with
                    | reference head tail =>
                        have same := Except.ok.inj (converted.symm.trans ((originalReference_projects objects _ _).mpr head))
                        subst_vars
                        exact ⟨_, (ih _).mpr tail, rfl⟩

/-- Every safe array has an ordered strong-reference conversion. -/
theorem strongReferences_succeeds (objects : Vector (Fin n) n) (cells : Array Format.Cell)
    (valid : ∀ cell ∈ cells.toList, Format.CellReferencesValid n cell) :
    ∃ output, strongReferences objects cells = .ok output ∧ ModelStrongReferences objects cells.toList output := by
  have existsProjection : ∀ (values : List Format.Cell),
      (∀ cell ∈ values, Format.CellReferencesValid n cell) → ∃ output, ModelStrongReferences objects values output := by
    intro values safe
    induction values with
    | nil => exact ⟨[], .nil⟩
    | cons cell rest ih =>
        obtain ⟨tail, projected⟩ := ih (fun cell member => safe cell (by simp [member]))
        cases cell with
        | immediate value => exact ⟨_, .immediate projected⟩
        | reference ref =>
            cases ref with
            | none => exact ⟨_, .null projected⟩
            | some id =>
                have cellValid : Format.ReferenceValid n (some id) := safe (.reference (some id)) (by simp)
                let index : Fin n := ⟨id.value, cellValid id (by simp)⟩
                exact ⟨objects.get index :: tail, .reference ⟨index, rfl, rfl⟩ projected⟩
  obtain ⟨output, projected⟩ := existsProjection cells.toList valid
  exact ⟨output, (strongReferences_correct _ _ _).mpr projected, projected⟩

/-- Generation, movement policy and destination placement are preserved independently. -/
def ModelOriginalKind : Format.ObjectState → Generational.OriginalKind → Prop
  | .youngOriginal .ordinary _, .youngMovable .ordinary => True
  | .youngOriginal .nonmoving _, .youngMovable .nonMoving => True
  | .oldOriginal .ordinary _, .oldMovable .ordinary => True
  | .oldOriginal .nonmoving _, .oldMovable .nonMoving => True
  | .nonmovingOriginal _, .nonmovingOld => True
  | _, _ => False

theorem originalKind_correct (before : Format.ObjectState) (after : Generational.OriginalKind) :
    originalKind before = .ok after ↔ ModelOriginalKind before after := by
  cases before with
  | youngOriginal policy header | oldOriginal policy header =>
      cases policy <;> cases after <;> simp [originalKind, ModelOriginalKind, pure, Except.pure]
      all_goals rename_i destination; cases destination <;> simp [originalKind.destination]
  | nonmovingOriginal marked | ordinaryDestination | nonmovingDestination =>
      cases after <;> simp [originalKind, ModelOriginalKind, pure, Except.pure]

/-- Every fresh state has its corresponding generation and movement kind. -/
theorem originalKind_succeeds (before : Format.ObjectState) (valid : Format.FreshState before) :
    ∃ after, originalKind before = .ok after ∧ ModelOriginalKind before after := by
  cases before with
  | youngOriginal policy header | oldOriginal policy header =>
      cases policy <;> exact ⟨_, rfl, trivial⟩
  | nonmovingOriginal marked => exact ⟨_, rfl, trivial⟩
  | ordinaryDestination | nonmovingDestination => cases valid


private theorem vector_mapM_success (values : Vector α n) (f : α → Except String β) (relation : α → β → Prop)
    (each : ∀ a ∈ values.toList, ∃ b, f a = .ok b ∧ relation a b) :
    ∃ output, values.mapM f = .ok output ∧ ListProjection relation values.toList output.toList := by
  obtain ⟨output, checked, projects⟩ := mapM_success values.toList f relation each
  have observed := congrArg (fun result : Except String (Array β) => Array.toList <$> result)
    (Vector.toArray_mapM (xs := values) (f := f))
  simp only [Functor.map_map, Vector.toList_toArray, Array.toList_mapM] at observed
  cases actual : values.mapM f with
  | error message => simp [actual, checked, Functor.map, Except.map] at observed
  | ok vector =>
      have same : vector.toList = output := by
        simpa only [actual, checked, Functor.map, Except.map, Except.ok.injEq] using observed
      exact ⟨vector, rfl, same ▸ projects⟩

/-- Canonical wire names select precisely their declared logical ordinal. -/
theorem modelObjectId_correct (count : Nat) (id : Format.ObjectId) (object : Fin count)
    (canonical : Format.CanonicalObjectId id) :
    modelObjectId count id = .ok object ↔ object.val = Format.logicalOrdinal id := by
  have parsed := canonical.2.2.2.2.1
  simp only [modelObjectId, parsed, required, bind, Except.bind, pure, Except.pure]
  exact checkedFin_correct _ _ _


end CloverGC.Differential
