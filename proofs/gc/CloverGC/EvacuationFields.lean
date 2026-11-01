import CloverGC.EvacuationSchedule

namespace CloverGC.Copying

/- Live fields: discovery preserves previous repairs, and writeback changes only
   the selected object's fields while preserving all metadata and allocations. -/
def fieldPlan (input : CopyInput n) (copying : CopyState n)
    (scanned : List (Fin n)) (object : Fin n) : List (Reference n) :=
  if object ∈ scanned then (input.heap.strongChildren object).map (canonical copying)
  else rawFields input object

/-- Pending objects retain raw source edges; scanned objects contain canonical
    edges. Movable original payloads are never rewritten. -/
structure FieldsInvariant (input : CopyInput n) (scanned : List (Fin n))
    (state : EvacuationState n) : Prop where
  originals : ∀ object, state.originalFields object =
    if input.movable object = false then fieldPlan input state.copying scanned object
    else rawFields input object
  copies : ∀ (index : Nat) (record : CopyRecord n), state.copying.copies[index]? = some record →
    record.strongChildren = fieldPlan input state.copying scanned record.origin

theorem fieldPlan_stable (input : CopyInput n) (before after : CopyState n)
    (scanned : List (Fin n))
    (stable : ∀ parent ∈ scanned, ∀ child ∈ input.heap.strongChildren parent,
      canonical before child = canonical after child) (object : Fin n) :
    fieldPlan input before scanned object = fieldPlan input after scanned object := by
  by_cases member : object ∈ scanned
  · simp only [fieldPlan, member, ↓reduceIte]
    exact List.map_congr_left (stable object member)
  · simp [fieldPlan, member]

theorem fields_discovery (input : CopyInput n) (state : EvacuationState n)
    (logical : MarkState n) (scanned : List (Fin n)) (object : Fin n)
    (h : CopyInvariant input state.copying) (s : Schedule input state.copying logical)
    (fields : FieldsInvariant input scanned state)
    (scanned_marked : ∀ x ∈ scanned, x ∈ logical.marked)
    (closed : ∀ parent ∈ scanned, ∀ child ∈ input.heap.strongChildren parent,
      child ∈ logical.marked) :
    FieldsInvariant input scanned
      { state with copying := (discoverReference input state.copying (.source object)).1 } := by
  let result := discoverReference input state.copying (.source object)
  have plan (x : Fin n) : fieldPlan input state.copying scanned x =
      fieldPlan input result.1 scanned x :=
    fieldPlan_stable input state.copying result.1 scanned
      (fun parent hp child hc =>
        (discovery_canonical_stable input state.copying logical (.source object)
          h s trivial child (closed parent hp child hc)).symm) x
  constructor
  · intro x
    simpa only [plan] using fields.originals x
  · intro index record get
    cases movable : input.movable object with
    | false =>
        have old : state.copying.copies[index]? = some record := by
          simpa [discoverReference, movable] using get
        exact (fields.copies index record old).trans (plan record.origin)
    | true =>
        cases forward : state.copying.forwarding object with
        | some _ =>
            have old : state.copying.copies[index]? = some record := by
              simpa [discoverReference, movable, forward] using get
            exact (fields.copies index record old).trans (plan record.origin)
        | none =>
            have appended : (state.copying.copies ++ [input.makeCopy object])[index]? = some record := by
              simpa [discoverReference, movable, forward, installCopy] using get
            rcases (append_record _ _ _ _).mp appended with old | ⟨rfl, rfl⟩
            · exact (fields.copies index record old).trans (plan record.origin)
            · have not_scanned : object ∉ scanned := by
                intro member
                have known := s.known object (scanned_marked object member)
                simp [canonical, forward, Represents, movable] at known
              simp [CopyInput.makeCopy, fieldPlan, not_scanned, rawFields]

theorem fields_repair (input : CopyInput n) (state : EvacuationState n)
    (logical : MarkState n) (scanned : List (Fin n)) (objects : List (Fin n))
    (h : CopyInvariant input state.copying) (s : Schedule input state.copying logical)
    (included : ∀ x ∈ logical.pending, x ∈ logical.marked)
    (fields : FieldsInvariant input scanned state)
    (scanned_marked : ∀ x ∈ scanned, x ∈ logical.marked)
    (closed : ∀ parent ∈ scanned, ∀ child ∈ input.heap.strongChildren parent,
      child ∈ logical.marked) :
    FieldsInvariant input scanned { state with copying :=
      (repairReferences input state.copying (objects.map Reference.source)).1 } := by
  induction objects generalizing state logical with
  | nil => exact fields
  | cons object rest ih =>
      have hnext := discovery_preserves input state.copying (.source object) h trivial
      have snext := discovery_schedule input state.copying logical object h s included
      have fnext := fields_discovery input state logical scanned object h s fields scanned_marked closed
      apply ih { state with copying := (discoverReference input state.copying (.source object)).1 }
        (discover object logical) hnext snext (discover_included logical object included) fnext
      · intro x hx
        exact (discover_marked object x logical).mpr (Or.inr (scanned_marked x hx))
      · intro parent hp child hc
        exact (discover_marked object child logical).mpr (Or.inr (closed parent hp child hc))

def writtenRecord (reference : Reference n) (fields : List (Reference n))
    (index : Nat) (record : CopyRecord n) : CopyRecord n :=
  if reference = .copied index then { record with strongChildren := fields } else record

@[simp] theorem written_origin (reference : Reference n) (fields : List (Reference n))
    (index : Nat) (record : CopyRecord n) :
    (writtenRecord reference fields index record).origin = record.origin := by
  unfold writtenRecord
  split <;> rfl

@[simp] theorem written_destination (reference : Reference n) (fields : List (Reference n))
    (index : Nat) (record : CopyRecord n) :
    (writtenRecord reference fields index record).destination = record.destination := by
  unfold writtenRecord
  split <;> rfl

@[simp] theorem write_get (state : EvacuationState n) (reference : Reference n)
    (fields : List (Reference n)) (index : Nat) :
    (writeFields state reference fields).copying.copies[index]? =
      (state.copying.copies[index]?).map (writtenRecord reference fields index) := by
  unfold writtenRecord
  cases reference <;> simp [writeFields, List.getElem?_modify]

@[simp] theorem write_forwarding (state : EvacuationState n) (reference : Reference n)
    (fields : List (Reference n)) :
    (writeFields state reference fields).copying.forwarding = state.copying.forwarding := by
  cases reference <;> rfl

@[simp] theorem write_marked (state : EvacuationState n) (reference : Reference n)
    (fields : List (Reference n)) :
    (writeFields state reference fields).copying.marked = state.copying.marked := by
  cases reference <;> rfl

@[simp] theorem write_pending (state : EvacuationState n) (reference : Reference n)
    (fields : List (Reference n)) :
    (writeFields state reference fields).copying.pending = state.copying.pending := by
  cases reference <;> rfl

@[simp] theorem write_roots (state : EvacuationState n) (reference : Reference n)
    (fields : List (Reference n)) :
    (writeFields state reference fields).roots = state.roots := by
  cases reference <;> rfl

@[simp] theorem write_canonical (state : EvacuationState n) (reference : Reference n)
    (fields : List (Reference n)) :
    canonical (writeFields state reference fields).copying = canonical state.copying := by
  funext object
  simp [canonical]

theorem write_original (state : EvacuationState n) (reference : Reference n)
    (fields : List (Reference n)) (object : Fin n) :
    (writeFields state reference fields).originalFields object =
      if reference = .source object then fields else state.originalFields object := by
  cases reference <;> simp [writeFields, eq_comm]

theorem write_preserves (input : CopyInput n) (state : EvacuationState n)
    (reference : Reference n) (fields : List (Reference n)) (h : CopyInvariant input state.copying) :
    CopyInvariant input (writeFields state reference fields).copying := by
  refine {
    ledger := ?_
    marked_canonical := ?_
    copies_marked := ?_
    marked_nodup := ?_
    pending_nodup := ?_
    pending_marked := ?_
  }
  · refine { forwarding_valid := ?_, copies_forwarded := ?_, copies_metadata := ?_ }
    · intro object index forward
      rw [write_forwarding] at forward
      obtain ⟨record, get, origin⟩ := h.ledger.forwarding_valid object index forward
      exact ⟨writtenRecord reference fields index record, by simp [get], by simpa using origin⟩
    · intro index record get
      rw [write_get, Option.map_eq_some_iff] at get
      obtain ⟨old, oldget, rfl⟩ := get
      simpa using h.ledger.copies_forwarded index old oldget
    · intro index record get
      rw [write_get, Option.map_eq_some_iff] at get
      obtain ⟨old, oldget, rfl⟩ := get
      simpa using h.ledger.copies_metadata index old oldget
  · intro ref marked
    rw [write_marked] at marked
    have valid := h.marked_canonical ref marked
    cases ref with
    | source _ => exact valid
    | copied index =>
        obtain ⟨record, get⟩ := valid
        exact ⟨writtenRecord reference fields index record, by simp [get]⟩
  · intro index record get
    rw [write_get, Option.map_eq_some_iff] at get
    obtain ⟨old, oldget, _⟩ := get
    simpa using h.copies_marked index old oldget
  · simpa using h.marked_nodup
  · simpa using h.pending_nodup
  · simpa using h.pending_marked

theorem write_schedule (input : CopyInput n) (state : EvacuationState n)
    (reference : Reference n) (fields : List (Reference n)) (logical : MarkState n)
    (s : Schedule input state.copying logical) :
    Schedule input (writeFields state reference fields).copying logical := by
  constructor
  · simpa using s.marked
  · simpa using s.pending
  · intro object marked
    have known := s.known object marked
    rw [write_canonical]
    cases eq : canonical state.copying object with
    | source _ => simpa [Represents, eq] using known
    | copied _ => simpa [Represents, eq] using known

theorem write_fields (input : CopyInput n) (state : EvacuationState n)
    (scanned : List (Fin n)) (parent : Fin n) (reference : Reference n)
    (h : CopyInvariant input state.copying) (fields : FieldsInvariant input scanned state)
    (rep : Represents input state.copying parent reference) :
    FieldsInvariant input (parent :: scanned)
      (writeFields state reference ((input.heap.strongChildren parent).map (canonical state.copying))) := by
  have source_eq (object : Fin n) : reference = .source object ↔
      input.movable object = false ∧ object = parent := by
    constructor
    · intro eq
      rw [eq] at rep
      exact ⟨by simpa [rep.1] using rep.2, rep.1⟩
    · rintro ⟨stationary, eq⟩
      subst object
      exact unique_representation input state.copying parent reference (.source parent) h rep ⟨rfl, stationary⟩
  constructor
  · intro object
    rw [write_original]
    simp only [fieldPlan, write_canonical]
    simp only [source_eq, fields.originals]
    by_cases eq : object = parent
    · subst object
      by_cases stationary : input.movable parent = false <;>
        simp [stationary]
    · simp [eq, fieldPlan]
  · intro index record get
    rw [write_get, Option.map_eq_some_iff] at get
    obtain ⟨old, oldget, rfl⟩ := get
    have copied_eq : reference = .copied index ↔ old.origin = parent := by
      constructor
      · intro eq
        exact no_source_merging input state.copying old.origin parent (.copied index) h
          (h.ledger.copies_forwarded index old oldget) (eq ▸ rep)
      · intro eq
        apply unique_representation input state.copying parent reference (.copied index) h rep
        simpa [Represents, eq] using h.ledger.copies_forwarded index old oldget
    simp only [written_origin, fieldPlan, write_canonical]
    by_cases eq : old.origin = parent
    · simp [writtenRecord, copied_eq.mpr eq, eq]
    · have absent : reference ≠ .copied index := fun equality => eq (copied_eq.mp equality)
      simp only [writtenRecord, absent, ↓reduceIte]
      simpa [fieldPlan, eq] using fields.copies index old oldget

end CloverGC.Copying
