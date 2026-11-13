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

/-- The retained physical-to-logical vector matches input object names slot by slot. -/
def PhysicalObjectMap (initial : Format.HeapSnapshot)
    (objects : Vector (Fin initial.objects.size) initial.objects.size) : Prop :=
  objects.toList.map Fin.val = initial.objects.toList.map (fun a => Format.logicalOrdinal a.objectId)

/-- Every valid identity permutation converts to the exact physical-to-logical map. -/
theorem modelObjectIds_succeeds (initial : Format.HeapSnapshot) (ids : Format.FreshIds initial) :
    ∃ objects, initial.objects.toVector.mapM (fun a => modelObjectId initial.objects.size a.objectId) = .ok objects ∧
      PhysicalObjectMap initial objects := by
  obtain ⟨objects, checked, projected⟩ := vector_mapM_success initial.objects.toVector
    (fun a => modelObjectId initial.objects.size a.objectId)
    (fun a object => object.val = Format.logicalOrdinal a.objectId) (by
      intro a member
      have inside : a ∈ initial.objects.toList := by simpa using member
      have bounded : Format.logicalOrdinal a.objectId < initial.objects.size :=
        List.mem_range.mp (ids.objects.mem_iff.mp (List.mem_map.mpr ⟨a, inside, rfl⟩))
      let object : Fin initial.objects.size := ⟨Format.logicalOrdinal a.objectId, bounded⟩
      exact ⟨object, (modelObjectId_correct _ _ _ (ids.canonical a inside)).mpr rfl, rfl⟩)
  refine ⟨objects, checked, ?_⟩
  unfold PhysicalObjectMap
  have columns : ∀ (before : List Format.Allocation) (after : List (Fin initial.objects.size)),
      ListProjection (fun a object => object.val = Format.logicalOrdinal a.objectId) before after →
      after.map Fin.val = before.map (fun a => Format.logicalOrdinal a.objectId) := by
    intro before after projection
    induction projection with
    | nil => rfl
    | cons head _ ih => simp [head, ih]
  simpa using columns _ _ projected

/-- Every processed identity stores its corresponding input value. Uniqueness
    ensures later writes cannot replace an earlier identity's record. -/
private structure IndexedTable (key : α → Fin n) (value : α → β) (processed : List α)
    (table : Vector (Option β) n) : Prop where
  unique : (processed.map key).Nodup
  stored : ∀ a ∈ processed, table.get (key a) = some (value a)

private theorem IndexedTable.empty (key : α → Fin n) (value : α → β) (table : Vector (Option β) n) :
    IndexedTable key value [] table := by
  constructor <;> simp

private theorem IndexedTable.insert {key : α → Fin n} {value : α → β} {processed : List α}
    {table : Vector (Option β) n} (progress : IndexedTable key value processed table)
    (a : α) (fresh : key a ∉ processed.map key) :
    IndexedTable key value (processed ++ [a]) (table.set (key a).val (some (value a)) (key a).isLt) := by
  constructor
  · rw [List.map_append, List.map_singleton, List.nodup_append]
    refine ⟨progress.unique, by simp, ?_⟩
    intro old member last lastMember equal
    simp only [List.mem_singleton] at lastMember
    exact fresh (lastMember ▸ equal ▸ member)
  · intro old member
    rcases List.mem_append.mp member with prior | last
    · have different : (key a).val ≠ (key old).val := by
        intro equal
        have same : key a = key old := Fin.ext equal
        exact fresh (same ▸ List.mem_map.mpr ⟨old, prior, rfl⟩)
      change (table.set (key a).val (some (value a)) (key a).isLt)[(key old).val] = some (value old)
      simp only [Vector.getElem_set, different, ↓reduceIte]
      exact progress.stored old prior
    · simp only [List.mem_singleton] at last
      subst old
      change (table.set (key a).val (some (value a)) (key a).isLt)[(key a).val] = some (value a)
      simp

/-- The construction loop stores every uniquely named input at its own slot. -/
private theorem buildIndexedTable_success (values : List α) (key : α → Fin n) (value : α → β)
    (before : Vector (Option β) n) (processed : List α)
    (progress : IndexedTable key value processed before)
    (unique : ((processed ++ values).map key).Nodup) :
    ∃ after, (forIn values before (fun a table =>
      pure (.yield (table.set (key a).val (some (value a)) (key a).isLt))) : Except String (Vector (Option β) n)) = .ok after ∧
      IndexedTable key value (processed ++ values) after := by
  induction values generalizing before processed with
  | nil => exact ⟨before, rfl, by simpa using progress⟩
  | cons a rest ih =>
      have absent : key a ∉ processed.map key := by
        intro member
        exact (List.nodup_append.mp (by simpa only [List.map_append] using unique)).2.2 _ member _ (by simp) rfl
      obtain ⟨after, done, final⟩ := ih _ (processed ++ [a]) (progress.insert a absent)
        (by simpa [List.append_assoc] using unique)
      refine ⟨after, ?_, ?_⟩
      · simpa only [List.forIn_cons, bind, Except.bind, pure, Except.pure] using done
      · simpa [List.append_assoc] using final

/-- Once all logical IDs are covered, no table lookup can fail. -/
private theorem IndexedTable.complete {key : α → Fin n} {value : α → β} {processed : List α}
    {table : Vector (Option β) n} (progress : IndexedTable key value processed table)
    (covered : ∀ index, index ∈ processed.map key) :
    ∀ index, ∃ record, table.get index = some record := by
  intro index
  obtain ⟨a, inside, same⟩ := List.mem_map.mp (covered index)
  exact ⟨value a, same ▸ progress.stored a inside⟩

/-- Complete identity coverage makes the executable option-table conversion total,
    with each stored record at its declared logical index. -/
private theorem IndexedTable.require_success {key : α → Fin n} {value : α → β} {processed : List α}
    {table : Vector (Option β) n} (progress : IndexedTable key value processed table)
    (covered : ∀ index, index ∈ processed.map key) (message : String) :
    ∃ records, table.mapM (required message) = .ok records ∧
      ∀ a ∈ processed, records.get (key a) = value a := by
  classical
  have filled := progress.complete covered
  let records : Vector β n := Vector.ofFn (fun i => Classical.choose (filled i))
  have lookup : ∀ i, table.get i = some (records.get i) := by
    intro i
    simpa [records, Vector.get] using Classical.choose_spec (filled i)
  have stored : table = records.map some := by
    apply Vector.ext
    intro i inside
    have slot := lookup ⟨i, inside⟩
    change table[i] = some records[i] at slot
    simpa using slot
  have checked : table.mapM (required message) = .ok records := by
    rw [stored, Vector.mapM_map]
    have convert : (required message ∘ some) = (fun a : β => (pure a : Except String β)) := rfl
    rw [convert, Vector.mapM_pure]
    simp [pure, Except.pure]
  refine ⟨records, checked, ?_⟩
  intro a inside
  exact (Option.some.inj ((progress.stored a inside).symm.trans (lookup (key a)))).symm


/-- Indexing retains every physical record at the logical ID named by the map. -/
theorem indexOriginals_succeeds (initial : Format.HeapSnapshot) (ids : Format.FreshIds initial)
    (objects : Vector (Fin initial.objects.size) initial.objects.size) (mapped : PhysicalObjectMap initial objects) :
    ∃ originals, indexOriginals initial.objects.toVector objects = .ok originals ∧
      ∀ pair ∈ (initial.objects.toVector.zip objects).toArray.toList,
        originals.get pair.2 = pair.1 := by
  let pairs := (initial.objects.toVector.zip objects).toArray.toList
  have keys : pairs.map Prod.snd = objects.toList := by
    simp only [pairs, Vector.toList_toArray, Vector.toList_zip]
    exact List.map_snd_zip (by simp)
  have numbered : (objects.toList.map Fin.val).Perm (List.range initial.objects.size) := by
    rw [mapped]
    exact ids.objects
  have unique : objects.toList.Nodup := by
    have values := numbered.symm.nodup List.nodup_range
    exact (List.pairwise_map.mp values).imp (fun different same => different (congrArg Fin.val same))
  have covered : ∀ index, index ∈ pairs.map Prod.snd := by
    intro index
    rw [keys]
    have member := numbered.mem_iff.mpr (List.mem_range.mpr index.isLt)
    obtain ⟨object, inside, same⟩ := List.mem_map.mp member
    have equal : object = index := Fin.ext same
    exact equal ▸ inside
  obtain ⟨table, built, progress⟩ := buildIndexedTable_success pairs Prod.snd Prod.fst
    (Vector.replicate initial.objects.size none) [] (IndexedTable.empty _ _ _)
    (by simpa [keys] using unique)
  obtain ⟨originals, checked, contents⟩ := progress.require_success covered "heap conversion: missing original"
  refine ⟨originals, ?_, ?_⟩
  · unfold indexOriginals
    dsimp only
    rw [← Array.forIn_toList]
    dsimp only [pairs] at built
    rw [built]
    exact checked
  · simpa only [List.nil_append] using contents


/-- Every wire unit's logical ID selects its corresponding model template. -/
def TemplatesIndexed (objects : Vector (Fin n) n) (units : List Format.WeakUnit)
    (templates : Fin k → Copying.Weak.Template n) : Prop :=
  ∀ unit ∈ units, ∃ index : Fin k, index.val = unit.unitId.value ∧
    ModelTemplate objects unit.contents (templates index)

/-- Vector specialization used by template-table construction. -/
def TemplateTable (objects : Vector (Fin n) n) (units : List Format.WeakUnit)
    (templates : Vector (Copying.Weak.Template n) units.length) : Prop :=
  TemplatesIndexed objects units templates.get

/-- Globally dense, unique unit IDs and safe contents fill every template slot. -/
theorem indexTemplates_succeeds (objects : Vector (Fin n) n) (units : List Format.WeakUnit)
    (dense : (units.map (fun unit => unit.unitId.value)).Perm (List.range units.length))
    (safe : ∀ unit ∈ units, Format.WeakContentsReferencesValid n unit.contents) :
    ∃ templates, indexTemplates objects units = .ok templates ∧ TemplateTable objects units templates := by
  classical
  let key (unit : { unit // unit ∈ units }) : Fin units.length :=
    ⟨unit.val.unitId.value, List.mem_range.mp
      (dense.mem_iff.mp (List.mem_map.mpr ⟨unit.val, unit.property, rfl⟩))⟩
  let data (unit : { unit // unit ∈ units }) : Copying.Weak.Template n :=
    Classical.choose (modelUnit_succeeds objects unit.val.contents (safe unit.val unit.property))
  have converts (unit : { unit // unit ∈ units }) :
      modelUnit objects unit.val.contents = .ok (data unit) ∧ ModelTemplate objects unit.val.contents (data unit) :=
    Classical.choose_spec (modelUnit_succeeds objects unit.val.contents (safe unit.val unit.property))
  have keyValues : units.attach.map (fun unit => (key unit).val) = units.map (fun unit => unit.unitId.value) := by
    simpa only [key] using (List.attach_map_val (l := units) (f := fun unit : Format.WeakUnit => unit.unitId.value))
  have unique : (units.attach.map key).Nodup := by
    have numbered : ((units.attach.map key).map Fin.val).Nodup := by
      simpa only [List.map_map, Function.comp_def, keyValues] using dense.symm.nodup List.nodup_range
    exact (List.pairwise_map.mp numbered).imp (fun different same => different (congrArg Fin.val same))
  have covered : ∀ index, index ∈ units.attach.map key := by
    intro index
    have member := dense.mem_iff.mpr (List.mem_range.mpr index.isLt)
    rw [← keyValues] at member
    obtain ⟨unit, inside, same⟩ := List.mem_map.mp member
    exact List.mem_map.mpr ⟨unit, inside, Fin.ext same⟩
  let before : Vector (Option (Copying.Weak.Template n)) units.length := Vector.replicate units.length none
  obtain ⟨table, built, progress⟩ := buildIndexedTable_success units.attach key data before []
    (IndexedTable.empty _ _ _) (by simpa using unique)
  obtain ⟨templates, checked, contents⟩ := progress.require_success covered "heap conversion: missing weak unit"
  let body (unit : Format.WeakUnit) (table : Vector (Option (Copying.Weak.Template n)) units.length) :
      Except String (ForInStep (Vector (Option (Copying.Weak.Template n)) units.length)) := do
    let id ← checkedFin unit.unitId.value units.length
    return .yield (table.set id.val (some (← modelUnit objects unit.contents)) id.isLt)
  have attached : units.attach.map Subtype.val = units := by
    simp
  have loop : forIn units before body = forIn units.attach before (fun unit table => body unit.val table) := by
    simpa only [attached] using (List.forIn_map (l := units.attach) (g := Subtype.val) (f := body) (init := before))
  have bodies : (fun (unit : { unit // unit ∈ units }) table => body unit.val table) =
      (fun unit table => (pure (.yield (table.set (key unit).val (some (data unit)) (key unit).isLt)) :
        Except String (ForInStep (Vector (Option (Copying.Weak.Template n)) units.length)))) := by
    funext unit table
    have idChecked : checkedFin unit.val.unitId.value units.length = .ok (key unit) :=
      (checkedFin_correct _ _ (key unit)).mpr rfl
    simp [body, idChecked, (converts unit).1, bind, Except.bind, pure, Except.pure]
  refine ⟨templates, ?_, ?_⟩
  · unfold indexTemplates
    dsimp only
    change (do let table ← forIn units before body
               table.mapM (required "heap conversion: missing weak unit")) = .ok templates
    rw [loop, bodies, built]
    exact checked
  · intro unit member
    let attachedUnit : { unit // unit ∈ units } := ⟨unit, member⟩
    refine ⟨key attachedUnit, rfl, ?_⟩
    rw [contents attachedUnit (List.mem_attach units attachedUnit)]
    exact (converts attachedUnit).2

/-- The format's owner/external unit domain satisfies template-table preconditions,
    independently of the concatenation order used to assemble the table. -/
theorem indexHeapTemplates_succeeds (initial : Format.HeapSnapshot) (valid : Format.FreshWellFormed initial)
    (objects : Vector (Fin initial.objects.size) initial.objects.size) :
    ∃ templates,
      indexTemplates objects (initial.objects.toList.flatMap (fun owner => owner.weak.toList) ++ initial.externalWeak.toList) = .ok templates ∧
      TemplateTable objects (initial.objects.toList.flatMap (fun owner => owner.weak.toList) ++ initial.externalWeak.toList) templates := by
  let units := initial.objects.toList.flatMap (fun owner => owner.weak.toList) ++ initial.externalWeak.toList
  have keys : (units.map (fun unit => unit.unitId.value)).Perm (Format.freshUnitIds initial) := by
    exact List.perm_append_comm.map _
  have length : (Format.freshUnitIds initial).length = units.length := by
    simpa only [List.length_map] using keys.length_eq.symm
  have dense : (units.map (fun unit => unit.unitId.value)).Perm (List.range units.length) := by
    simpa only [length] using keys.trans valid.units
  have safe : ∀ unit ∈ units, Format.WeakContentsReferencesValid initial.objects.size unit.contents := by
    intro unit member
    rcases List.mem_append.mp member with owned | external
    · obtain ⟨owner, inside, member⟩ := List.mem_flatMap.mp owned
      exact (valid.references.objects owner inside).2 unit member
    · exact valid.references.externalWeak unit external
  exact indexTemplates_succeeds objects units dense safe


/-- Unit-ID conversion preserves the complete list, including order and duplicates. -/
def ModelUnitIds (units : List Format.WeakUnit) (ids : List (Fin k)) : Prop :=
  ids.map Fin.val = units.map (fun unit => unit.unitId.value)

theorem modelUnitIds_correct (count : Nat) (units : List Format.WeakUnit) (ids : List (Fin count)) :
    modelUnitIds count units = .ok ids ↔ ModelUnitIds units ids := by
  unfold modelUnitIds ModelUnitIds
  induction units generalizing ids with
  | nil => cases ids <;> simp [pure, Except.pure]
  | cons unit rest ih =>
      cases ids with
      | nil => simp [List.mapM_cons, bind_ok, pure, Except.pure]
      | cons id tail =>
          simp [List.mapM_cons, bind_ok, pure, Except.pure, checkedFin_correct, ih]

/-- Every in-range unit list converts without deleting or reordering a location. -/
theorem modelUnitIds_succeeds (count : Nat) (units : List Format.WeakUnit)
    (bounded : ∀ unit ∈ units, unit.unitId.value < count) :
    ∃ ids, modelUnitIds count units = .ok ids ∧ ModelUnitIds units ids := by
  let ids := units.attach.map (fun unit : { unit // unit ∈ units } =>
    (⟨unit.val.unitId.value, bounded unit.val unit.property⟩ : Fin count))
  have projected : ModelUnitIds units ids := by simp [ModelUnitIds, ids]
  exact ⟨ids, (modelUnitIds_correct _ _ _).mpr projected, projected⟩

/-- Remembered-owner conversion preserves physical identity through the retained
    pairing and retains the input list order. -/
def ModelRememberedOwners (objects : Vector (Fin n) n)
    (owners : List Format.AllocationId) (output : List (Fin n)) : Prop :=
  ListProjection (fun id object => ModelReference objects (some id) (some object)) owners output

theorem modelRememberedOwners_succeeds (initial : Format.HeapSnapshot)
    (objects : Vector (Fin initial.objects.size) initial.objects.size)
    (valid : Format.RememberedOwnersValid initial) :
    ∃ owners, initial.remembered.toList.mapM (originalReference objects) = .ok owners ∧
      ModelRememberedOwners objects initial.remembered.toList owners := by
  apply mapM_success
  intro id member
  obtain ⟨owner, lookup, _⟩ := valid id member
  have bounded : id.value < initial.objects.size := by
    obtain ⟨inside, _⟩ := Array.getElem?_eq_some_iff.mp lookup
    exact inside
  let index : Fin initial.objects.size := ⟨id.value, bounded⟩
  have projects : ModelReference objects (some id) (some (objects.get index)) := ⟨index, rfl, rfl⟩
  exact ⟨objects.get index, (originalReference_projects _ _ _).mpr projects, projects⟩


private theorem vector_mapM_at (values : Vector α n) (f : α → Except String β) (relation : α → β → Prop)
    (each : ∀ a ∈ values.toList, ∃ b, f a = .ok b ∧ relation a b) :
    ∃ output, values.mapM f = .ok output ∧ ∀ index, relation (values.get index) (output.get index) := by
  obtain ⟨output, checked, projected⟩ := vector_mapM_success values f relation each
  refine ⟨output, checked, ?_⟩
  intro index
  have atIndex := projected.getElem index.val (by simp) (by simp)
  change relation values[index.val] output[index.val]
  simpa only [Vector.getElem_toList] using atIndex

/-- Every logical original's strong fields convert at the same vector index. -/
theorem modelChildren_succeeds (objects : Vector (Fin n) n) (originals : Vector Format.Allocation n)
    (safe : ∀ a ∈ originals.toList, ∀ cell ∈ a.strong.toList, Format.CellReferencesValid n cell) :
    ∃ children, originals.mapM (fun a => strongReferences objects a.strong) = .ok children ∧
      ∀ owner, ModelStrongReferences objects (originals.get owner).strong.toList (children.get owner) := by
  apply vector_mapM_at originals (fun a => strongReferences objects a.strong)
    (fun a children => ModelStrongReferences objects a.strong.toList children)
  intro a member
  exact strongReferences_succeeds objects a.strong (safe a member)

/-- Every fresh original's generation and placement convert at the same vector index. -/
theorem modelKinds_succeeds (originals : Vector Format.Allocation n)
    (fresh : ∀ a ∈ originals.toList, Format.FreshState a.state) :
    ∃ kinds, originals.mapM (fun a => originalKind a.state) = .ok kinds ∧
      ∀ owner, ModelOriginalKind (originals.get owner).state (kinds.get owner) := by
  apply vector_mapM_at originals (fun a => originalKind a.state)
    (fun a kind => ModelOriginalKind a.state kind)
  intro a member
  exact originalKind_succeeds a.state (fresh a member)

/-- Owner location lists convert in place, retaining both owner and unit order. -/
theorem modelOwnerUnits_succeeds (count : Nat) (originals : Vector Format.Allocation n)
    (bounded : ∀ a ∈ originals.toList, ∀ unit ∈ a.weak.toList, unit.unitId.value < count) :
    ∃ ownerUnits, originals.mapM (fun a => modelUnitIds count a.weak.toList) = .ok ownerUnits ∧
      ∀ owner, ModelUnitIds (originals.get owner).weak.toList (ownerUnits.get owner) := by
  apply vector_mapM_at originals (fun a => modelUnitIds count a.weak.toList)
    (fun a ids => ModelUnitIds a.weak.toList ids)
  intro a member
  exact modelUnitIds_succeeds count a.weak.toList (bounded a member)

end CloverGC.Differential
