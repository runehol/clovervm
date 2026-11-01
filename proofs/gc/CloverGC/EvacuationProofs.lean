import CloverGC.Evacuation
import CloverGC.CopyDiscoveryProofs
import CloverGC.MarkProofs

namespace CloverGC.Copying

/- Discovery/queue simulation: establish the relationship to abstract marking. -/
theorem represents_canonical (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (reference : Reference n) (h : CopyInvariant input state)
    (represents : Represents input state object reference) : canonical state object = reference := by
  cases reference with
  | source other =>
      obtain ⟨rfl, nonmoving⟩ := represents
      simp [canonical, nonmoving_unforwarded input state other h nonmoving]
  | copied index =>
      change state.forwarding object = some index at represents
      simp [canonical, represents]

theorem discovery_represents_mono (input : CopyInput n) (state : CopyState n)
    (incoming reference : Reference n) (object : Fin n)
    (represents : Represents input state object reference) :
    Represents input (discoverReference input state incoming).1 object reference := by
  cases reference with
  | source _ => exact represents
  | copied index => exact discovery_preserves_forwarding input state incoming object index represents

/-- A lockstep relation to the already-proved abstract marking algorithm. -/
structure Schedule (input : CopyInput n) (copying : CopyState n)
    (logical : MarkState n) : Prop where
  marked : copying.marked = logical.marked.map (canonical copying)
  pending : copying.pending = logical.pending.map (canonical copying)
  known : ∀ object ∈ logical.marked,
    Represents input copying object (canonical copying object)

theorem schedule_empty (input : CopyInput n) : Schedule input .empty .empty := by
  constructor <;> simp [CopyState.empty, MarkState.empty]

theorem schedule_member (input : CopyInput n) (state : CopyState n) (logical : MarkState n)
    (h : CopyInvariant input state) (s : Schedule input state logical)
    (object : Fin n) (reference : Reference n)
    (represents : Represents input state object reference) :
    reference ∈ state.marked ↔ object ∈ logical.marked := by
  constructor
  · intro member
    rw [s.marked, List.mem_map] at member
    obtain ⟨other, marked, eq⟩ := member
    have same := no_source_merging input state other object reference h
      (eq ▸ s.known other marked) represents
    simpa [same] using marked
  · intro member
    rw [s.marked]
    exact List.mem_map.mpr ⟨object, member,
      represents_canonical input state object reference h represents⟩

theorem discover_included (logical : MarkState n) (object : Fin n)
    (included : ∀ x ∈ logical.pending, x ∈ logical.marked) :
    ∀ x ∈ (discover object logical).pending, x ∈ (discover object logical).marked := by
  intro x hx
  rcases (discover_pending object x logical).mp hx with old | ⟨rfl, _⟩
  · exact (discover_marked object x logical).mpr (Or.inr (included x old))
  · exact (discover_marked _ _ logical).mpr (Or.inl rfl)

theorem discovery_canonical_stable (input : CopyInput n) (state : CopyState n)
    (logical : MarkState n) (incoming : Reference n)
    (h : CopyInvariant input state) (s : Schedule input state logical)
    (valid : ValidReference state incoming) (object : Fin n) (marked : object ∈ logical.marked) :
    canonical (discoverReference input state incoming).1 object = canonical state object := by
  exact represents_canonical input _ object _ (discovery_preserves input state incoming h valid)
    (discovery_represents_mono input state incoming _ object (s.known object marked))

/-- Source discovery changes the physical queue exactly as abstract discovery
    changes the logical queue. Forwarding preserves the meaning of old marks. -/
theorem discovery_schedule (input : CopyInput n) (state : CopyState n) (logical : MarkState n)
    (object : Fin n) (h : CopyInvariant input state) (s : Schedule input state logical)
    (included : ∀ x ∈ logical.pending, x ∈ logical.marked) :
    Schedule input (discoverReference input state (.source object)).1
      (discover object logical) := by
  let result := discoverReference input state (.source object)
  have newh := discovery_preserves input state (.source object) h trivial
  have rep := discovery_source_represents input state object
  have result_eq : result.2 = canonical result.1 object :=
    (represents_canonical input result.1 object result.2 newh rep).symm
  have stable (x : Fin n) (hx : x ∈ logical.marked) :
      canonical result.1 x = canonical state x :=
    discovery_canonical_stable input state logical (.source object) h s trivial x hx
  by_cases known : object ∈ logical.marked
  · have member := (schedule_member input state logical h s object _ (s.known object known)).mpr known
    have same := represented_source_rediscovery input state object _ h (s.known object known) member
    simpa [same, discover, known] using s
  · have unforwarded : state.forwarding object = none := by
      cases forward : state.forwarding object with
      | none => rfl
      | some index =>
          obtain ⟨record, get, _⟩ := h.ledger.forwarding_valid object index forward
          have member := h.copies_marked index record get
          exact False.elim (known ((schedule_member input state logical h s object
            (.copied index) forward).mp member))
    have effects : result.1.marked = result.2 :: state.marked ∧
        result.1.pending = state.pending ++ [result.2] := by
      cases movable : input.movable object with
      | false =>
          have absent : Reference.source object ∉ state.marked := by
            intro member
            exact known ((schedule_member input state logical h s object (.source object)
              ⟨rfl, movable⟩).mp member)
          simp [result, discoverReference, movable, enqueueOnce, absent]
      | true =>
          have absent : Reference.copied state.copies.length ∉ state.marked := by
            intro member
            obtain ⟨record, get⟩ := h.marked_canonical _ member
            simp at get
          simp [result, discoverReference, movable, unforwarded, installCopy, enqueueOnce, absent]
    constructor
    · simp only [discover, known, ↓reduceIte, List.map_cons]
      rw [effects.1, s.marked, result_eq]
      exact congrArg (canonical result.1 object :: ·) (List.map_congr_left (fun x hx => (stable x hx).symm))
    · simp only [discover, known, ↓reduceIte, List.map_append, List.map_cons, List.map_nil]
      rw [effects.2, s.pending, result_eq]
      exact congrArg (· ++ [canonical result.1 object])
        (List.map_congr_left (fun x hx => (stable x (included x hx)).symm))
    · intro x hx
      rcases (discover_marked object x logical).mp hx with rfl | hx
      · rw [← result_eq]
        exact rep
      · rw [stable x hx]
        exact discovery_represents_mono input state (.source object) _ x (s.known x hx)

theorem repair_preserves (input : CopyInput n) (state : CopyState n) (objects : List (Fin n))
    (h : CopyInvariant input state) :
    CopyInvariant input (repairReferences input state (objects.map Reference.source)).1 := by
  induction objects generalizing state with
  | nil => exact h
  | cons object rest ih =>
      exact ih _ (discovery_preserves input state (.source object) h trivial)

theorem repair_canonical_stable (input : CopyInput n) (state : CopyState n)
    (objects : List (Fin n)) (logical : MarkState n)
    (h : CopyInvariant input state) (s : Schedule input state logical)
    (included : ∀ x ∈ logical.pending, x ∈ logical.marked)
    (object : Fin n) (marked : object ∈ logical.marked) :
    canonical (repairReferences input state (objects.map Reference.source)).1 object =
      canonical state object := by
  induction objects generalizing state logical with
  | nil => rfl
  | cons next rest ih =>
      have hnext := discovery_preserves input state (.source next) h trivial
      have snext := discovery_schedule input state logical next h s included
      have mnext := (discover_marked next object logical).mpr (Or.inr marked)
      have tail := ih _ _ hnext snext (discover_included logical next included) mnext
      exact tail.trans (discovery_canonical_stable input state logical (.source next) h s trivial object marked)

theorem repair_schedule (input : CopyInput n) (state : CopyState n) (logical : MarkState n)
    (objects : List (Fin n)) (h : CopyInvariant input state) (s : Schedule input state logical)
    (included : ∀ x ∈ logical.pending, x ∈ logical.marked) :
    Schedule input (repairReferences input state (objects.map Reference.source)).1
      (discoverMany objects logical) := by
  induction objects generalizing state logical with
  | nil => exact s
  | cons object rest ih =>
      exact ih _ _ (discovery_preserves input state (.source object) h trivial)
        (discovery_schedule input state logical object h s included)
        (discover_included logical object included)

theorem repair_outputs (input : CopyInput n) (state : CopyState n) (logical : MarkState n)
    (objects : List (Fin n)) (h : CopyInvariant input state) (s : Schedule input state logical)
    (included : ∀ x ∈ logical.pending, x ∈ logical.marked) :
    (repairReferences input state (objects.map Reference.source)).2 =
      objects.map (canonical (repairReferences input state (objects.map Reference.source)).1) := by
  induction objects generalizing state logical with
  | nil => rfl
  | cons object rest ih =>
      have hn := discovery_preserves input state (.source object) h trivial
      have sn := discovery_schedule input state logical object h s included
      have tail := ih _ _ hn sn (discover_included logical object included)
      have stable := repair_canonical_stable input _ rest _ hn sn
        (discover_included logical object included) object
        ((discover_marked object object logical).mpr (Or.inl rfl))
      have head := represents_canonical input _ object _ hn (discovery_source_represents input state object)
      simp only [List.map_cons, repairReferences]
      exact congr (congrArg List.cons (head.symm.trans stable.symm)) tail

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
  constructor
  · constructor
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

theorem plan_cons (input : CopyInput n) (copying : CopyState n)
    (scanned : List (Fin n)) (parent object : Fin n) :
    fieldPlan input copying (parent :: scanned) object =
      if object = parent then (input.heap.strongChildren object).map (canonical copying)
      else fieldPlan input copying scanned object := by
  by_cases eq : object = parent <;> simp [fieldPlan, eq]

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

/- Assemble the invariant, prove seeding, then prove one complete scan. -/
structure EvacuationInvariant (input : CopyInput n) (roots scanned : List (Fin n))
    (logical : MarkState n) (state : EvacuationState n) : Prop where
  copying : CopyInvariant input state.copying
  schedule : Schedule input state.copying logical
  marking : CloverGC.Invariant input.heap roots scanned logical
  fields : FieldsInvariant input scanned state
  roots_marked : ∀ object ∈ roots, object ∈ logical.marked
  roots_repaired : state.roots = roots.map (canonical state.copying)

theorem seed_evacuation (input : CopyInput n) (roots : List (Fin n)) :
    EvacuationInvariant input roots [] (seed roots) (seedRoots input roots) := by
  let initial : EvacuationState n := ⟨.empty, rawFields input, []⟩
  have initialFields : FieldsInvariant input [] initial := by
    constructor
    · intro object
      simp [initial, fieldPlan]
    · simp [initial, CopyState.empty]
  have f := fields_repair input initial .empty [] roots (empty_invariant input)
    (schedule_empty input) (by simp [MarkState.empty]) initialFields
    (by simp) (by simp)
  refine {
    copying := repair_preserves input .empty roots (empty_invariant input)
    schedule := repair_schedule input .empty .empty roots (empty_invariant input)
      (schedule_empty input) (by simp [MarkState.empty])
    marking := seed_invariant input.heap roots
    fields := ⟨f.originals, f.copies⟩
    roots_marked := ?_
    roots_repaired := repair_outputs input .empty .empty roots (empty_invariant input)
      (schedule_empty input) (by simp [MarkState.empty])
  }
  intro object member
  exact (discoverMany_marked roots .empty object).mpr (Or.inr member)

theorem liveFields_plan (input : CopyInput n) (state : EvacuationState n)
    (scanned : List (Fin n)) (object : Fin n) (reference : Reference n)
    (h : CopyInvariant input state.copying) (fields : FieldsInvariant input scanned state)
    (rep : Represents input state.copying object reference) :
    liveFields state reference = fieldPlan input state.copying scanned object := by
  cases reference with
  | source other =>
      obtain ⟨rfl, stationary⟩ := rep
      simpa [liveFields, stationary] using fields.originals other
  | copied index =>
      obtain ⟨record, get, origin⟩ := h.ledger.forwarding_valid object index rep
      simpa [liveFields, get, origin] using fields.copies index record get

theorem pop_preserves (input : CopyInput n) (copying : CopyState n)
    (reference : Reference n) (rest : List (Reference n)) (h : CopyInvariant input copying)
    (queue : copying.pending = reference :: rest) :
    CopyInvariant input { copying with pending := rest } := by
  refine {
    ledger := ⟨h.ledger.forwarding_valid, h.ledger.copies_forwarded, h.ledger.copies_metadata⟩
    marked_canonical := h.marked_canonical
    copies_marked := h.copies_marked
    marked_nodup := h.marked_nodup
    pending_nodup := ?_
    pending_marked := ?_
  }
  · have nodup := h.pending_nodup
    rw [queue, List.nodup_cons] at nodup
    exact nodup.2
  · intro x hx
    exact h.pending_marked x (by simp [queue, hx])

/-- A physical scan reads the current raw fields and matches one abstract scan.
    Writeback is into the post-discovery state, so new allocations survive. -/
theorem scan_evacuation (input : CopyInput n) (roots scanned : List (Fin n))
    (logical : MarkState n) (state : EvacuationState n) (object : Fin n)
    (rest : List (Fin n)) (h : EvacuationInvariant input roots scanned logical state)
    (queue : logical.pending = object :: rest) :
    ∃ next, scanCopy input state = some (canonical state.copying object, next) ∧
      EvacuationInvariant input roots (object :: scanned)
        (discoverMany (input.heap.strongChildren object) ⟨logical.marked, rest⟩) next ∧
      ∀ x ∈ logical.marked, canonical next.copying x = canonical state.copying x := by
  let reference := canonical state.copying object
  let popped : CopyState n := { state.copying with pending := rest.map (canonical state.copying) }
  let logicalPopped : MarkState n := ⟨logical.marked, rest⟩
  let repaired := repairReferences input popped ((input.heap.strongChildren object).map Reference.source)
  let prepared : EvacuationState n := { state with copying := repaired.1 }
  let next := writeFields prepared reference repaired.2
  have physicalQueue : state.copying.pending = reference :: rest.map (canonical state.copying) := by
    simp [h.schedule.pending, queue, reference]
  have objectMarked : object ∈ logical.marked :=
    (h.marking.accounted object).mpr (Or.inl (by simp [queue]))
  have notScanned : object ∉ scanned := h.marking.disjoint object (by simp [queue])
  have raw : liveFields state reference = rawFields input object := by
    rw [liveFields_plan input state scanned object reference h.copying h.fields
      (h.schedule.known object objectMarked)]
    simp [fieldPlan, notScanned]
  have hp := pop_preserves input state.copying reference _ h.copying physicalQueue
  have sp : Schedule input popped logicalPopped := {
    marked := h.schedule.marked
    pending := rfl
    known := h.schedule.known
  }
  have included : ∀ x ∈ logicalPopped.pending, x ∈ logicalPopped.marked := by
    intro x hx
    exact (h.marking.accounted x).mpr (Or.inl (by simp [queue, logicalPopped] at hx ⊢; exact Or.inr hx))
  have poppedFields : FieldsInvariant input scanned { state with copying := popped } :=
    ⟨h.fields.originals, h.fields.copies⟩
  have scannedMarked : ∀ x ∈ scanned, x ∈ logicalPopped.marked := by
    intro x hx
    exact (h.marking.accounted x).mpr (Or.inr hx)
  have f := fields_repair input { state with copying := popped } logicalPopped scanned
    (input.heap.strongChildren object) hp sp included poppedFields scannedMarked h.marking.closed
  have hn := repair_preserves input popped (input.heap.strongChildren object) hp
  have sn := repair_schedule input popped logicalPopped (input.heap.strongChildren object) hp sp included
  have output := repair_outputs input popped logicalPopped (input.heap.strongChildren object) hp sp included
  have stable (x : Fin n) (hx : x ∈ logical.marked) : canonical repaired.1 x = canonical state.copying x :=
    repair_canonical_stable input popped (input.heap.strongChildren object) logicalPopped hp sp included x hx
  have newObjectMarked : object ∈ (discoverMany (input.heap.strongChildren object) logicalPopped).marked :=
    (discoverMany_marked _ _ _).mpr (Or.inl objectMarked)
  have rep : Represents input repaired.1 object reference := by
    have known := sn.known object newObjectMarked
    rw [stable object objectMarked] at known
    exact known
  refine ⟨next, ?_, ?_, ?_⟩
  · simp [scanCopy, physicalQueue, raw, rawFields, next, prepared, repaired, popped, reference]
  · refine {
      copying := write_preserves input prepared reference repaired.2 hn
      schedule := write_schedule input prepared reference repaired.2 _ sn
      marking := scan_preserves input.heap roots scanned logical object rest h.marking queue
      fields := ?_
      roots_marked := ?_
      roots_repaired := ?_
    }
    · change FieldsInvariant input (object :: scanned) (writeFields prepared reference repaired.2)
      have output' : repaired.2 = (input.heap.strongChildren object).map (canonical repaired.1) := output
      rw [output']
      exact write_fields input prepared scanned object reference hn f rep
    · intro x hx
      exact (discoverMany_marked _ _ _).mpr (Or.inl (h.roots_marked x hx))
    · rw [write_roots, write_canonical]
      exact h.roots_repaired.trans (List.map_congr_left (fun x hx => (stable x (h.roots_marked x hx)).symm))
  · intro x hx
    exact (congrFun (write_canonical prepared reference repaired.2) x).trans (stable x hx)

/-- Simulation is proved for arbitrary fuel; completion will follow from the
    mark theorem, not from assuming the evacuation queue becomes empty. -/
theorem drain_evacuation (input : CopyInput n) (roots scanned : List (Fin n))
    (fuel : Nat) (logical : MarkState n) (state : EvacuationState n)
    (h : EvacuationInvariant input roots scanned logical state) :
    let lr := drain input.heap fuel logical
    let pr := drainCopies input fuel state
    EvacuationInvariant input roots (lr.2.reverse ++ scanned) lr.1 pr.1 ∧
    pr.2 = lr.2.map (canonical pr.1.copying) ∧
    ∀ x ∈ logical.marked, canonical pr.1.copying x = canonical state.copying x := by
  induction fuel generalizing scanned logical state with
  | zero =>
      exact ⟨by simpa [drain, drainCopies] using h, rfl, by intro x hx; rfl⟩
  | succ fuel ih =>
      cases queue : logical.pending with
      | nil =>
          have physical : state.copying.pending = [] := by simp [h.schedule.pending, queue]
          simpa [drain, scanOne, queue, drainCopies, scanCopy, physical] using
            (show EvacuationInvariant input roots scanned logical state ∧
              ([] : List (Reference n)) = [] ∧
              (∀ x ∈ logical.marked, canonical state.copying x = canonical state.copying x) from
                ⟨h, rfl, by intro x hx; rfl⟩)
      | cons object rest =>
          obtain ⟨next, step, nextInvariant, stepStable⟩ :=
            scan_evacuation input roots scanned logical state object rest h queue
          obtain ⟨finalInvariant, trace, tailStable⟩ :=
            ih (object :: scanned) _ next nextInvariant
          have oldMarked : object ∈ logical.marked :=
            (h.marking.accounted object).mpr (Or.inl (by simp [queue]))
          have newMarked : object ∈ (discoverMany (input.heap.strongChildren object)
              (⟨logical.marked, rest⟩ : MarkState n)).marked :=
            (discoverMany_marked _ _ _).mpr (Or.inl oldMarked)
          simp only [drain, scanOne, queue, drainCopies, step]
          refine ⟨?_, ?_, ?_⟩
          · simpa [List.reverse_cons, List.append_assoc] using finalInvariant
          · exact congr (congrArg List.cons ((tailStable object newMarked).trans
              (stepStable object oldMarked)).symm) trace
          · intro x hx
            exact (tailStable x ((discoverMany_marked _ _ _).mpr (Or.inl hx))).trans
              (stepStable x hx)

theorem evacuate_invariant (input : CopyInput n) (roots : List (Fin n)) :
    EvacuationInvariant input roots (collect input.heap roots).2.reverse
      (collect input.heap roots).1 (evacuate input roots).1 := by
  have result := (drain_evacuation input roots [] n (seed roots) (seedRoots input roots)
    (seed_evacuation input roots)).1
  simpa [collect, evacuate] using result

theorem evacuate_trace (input : CopyInput n) (roots : List (Fin n)) :
    (evacuate input roots).2 =
      (collect input.heap roots).2.map (canonical (evacuate input roots).1.copying) := by
  exact (drain_evacuation input roots [] n (seed roots) (seedRoots input roots)
    (seed_evacuation input roots)).2.1

theorem evacuate_complete (input : CopyInput n) (roots : List (Fin n)) :
    (evacuate input roots).1.copying.pending = [] := by
  have inv := evacuate_invariant input roots
  simp [inv.schedule.pending, collect_complete]

theorem canonical_injective (input : CopyInput n) (copying : CopyState n)
    (h : CopyInvariant input copying) (left right : Fin n)
    (eq : canonical copying left = canonical copying right) : left = right := by
  cases hl : copying.forwarding left with
  | none =>
      cases hr : copying.forwarding right with
      | none => simpa [canonical, hl, hr] using eq
      | some _ => simp [canonical, hl, hr] at eq
  | some li =>
      cases hr : copying.forwarding right with
      | none => simp [canonical, hl, hr] at eq
      | some ri =>
          have same : li = ri := by simpa [canonical, hl, hr] using eq
          subst ri
          exact no_source_merging input copying left right (.copied li) h hl hr

/-- Exactly the reachable logical objects have marked canonical objects. -/
theorem evacuate_reachable (input : CopyInput n) (roots : List (Fin n)) (object : Fin n) :
    canonical (evacuate input roots).1.copying object ∈ (evacuate input roots).1.copying.marked ↔
      Reachable input.heap roots object := by
  have inv := evacuate_invariant input roots
  rw [inv.schedule.marked, List.mem_map]
  constructor
  · rintro ⟨other, member, eq⟩
    have same := canonical_injective input _ inv.copying other object eq
    exact (collect_correct input.heap roots object).mp (same ▸ member)
  · intro reachable
    exact ⟨object, (collect_correct input.heap roots object).mpr reachable, rfl⟩

theorem evacuate_represents (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (reachable : Reachable input.heap roots object) :
    Represents input (evacuate input roots).1.copying object
      (canonical (evacuate input roots).1.copying object) := by
  exact (evacuate_invariant input roots).schedule.known object
    ((collect_correct input.heap roots object).mpr reachable)

theorem evacuate_roots (input : CopyInput n) (roots : List (Fin n)) :
    (evacuate input roots).1.roots = roots.map (canonical (evacuate input roots).1.copying) :=
  (evacuate_invariant input roots).roots_repaired

/-- Slot order and multiplicity are preserved, not just edge-set reachability. -/
theorem evacuate_children (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (reachable : Reachable input.heap roots object) :
    liveFields (evacuate input roots).1 (canonical (evacuate input roots).1.copying object) =
      (input.heap.strongChildren object).map (canonical (evacuate input roots).1.copying) := by
  have inv := evacuate_invariant input roots
  have marked := (collect_correct input.heap roots object).mpr reachable
  have scanned := (inv.marking.accounted object).mp marked
  simp only [collect_complete, List.not_mem_nil, false_or] at scanned
  rw [liveFields_plan input _ _ object _ inv.copying inv.fields
    (evacuate_represents input roots object reachable)]
  simp [fieldPlan, scanned]

theorem evacuate_forwarded_iff (input : CopyInput n) (roots : List (Fin n)) (object : Fin n) :
    (∃ index, (evacuate input roots).1.copying.forwarding object = some index) ↔
      Reachable input.heap roots object ∧ input.movable object = true := by
  have inv := evacuate_invariant input roots
  constructor
  · rintro ⟨index, forward⟩
    obtain ⟨record, get, origin⟩ := inv.copying.ledger.forwarding_valid object index forward
    have movable := (inv.copying.ledger.copies_metadata index record get).1
    have marked := (schedule_member input _ _ inv.copying inv.schedule object (.copied index)
      forward).mp (inv.copying.copies_marked index record get)
    exact ⟨(collect_correct input.heap roots object).mp marked, origin ▸ movable⟩
  · rintro ⟨reachable, movable⟩
    have rep := evacuate_represents input roots object reachable
    cases forward : (evacuate input roots).1.copying.forwarding object with
    | none => simp [canonical, forward, Represents, movable] at rep
    | some index => exact ⟨index, rfl⟩

theorem evacuate_nonmoving (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (stationary : input.movable object = false) :
    canonical (evacuate input roots).1.copying object = .source object := by
  simp [canonical, nonmoving_unforwarded input _ object (evacuate_invariant input roots).copying stationary]

theorem evacuate_source_unchanged (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (movable : input.movable object = true) :
    (evacuate input roots).1.originalFields object = rawFields input object := by
  simpa [movable] using (evacuate_invariant input roots).fields.originals object

/-- All references that survive collection name allocated copies or NonMoving
    originals. Unreachable garbage and discarded source payloads are excluded. -/
theorem evacuate_disposal_safe (input : CopyInput n) (roots : List (Fin n)) :
    (∀ reference ∈ (evacuate input roots).1.roots,
      Canonical input (evacuate input roots).1.copying reference) ∧
    (∀ object, Reachable input.heap roots object →
      ∀ reference ∈ liveFields (evacuate input roots).1
          (canonical (evacuate input roots).1.copying object),
        Canonical input (evacuate input roots).1.copying reference) := by
  have inv := evacuate_invariant input roots
  have canonical_live (object : Fin n) (reachable : Reachable input.heap roots object) :
      Canonical input (evacuate input roots).1.copying
        (canonical (evacuate input roots).1.copying object) :=
    inv.copying.marked_canonical _ ((evacuate_reachable input roots object).mpr reachable)
  constructor
  · intro reference member
    rw [evacuate_roots, List.mem_map] at member
    obtain ⟨object, root, rfl⟩ := member
    exact canonical_live object (Reachable.root root)
  · intro object reachable reference member
    rw [evacuate_children input roots object reachable, List.mem_map] at member
    obtain ⟨child, edge, rfl⟩ := member
    exact canonical_live child (Reachable.edge reachable edge)

theorem evacuate_trace_nodup (input : CopyInput n) (roots : List (Fin n)) :
    (evacuate input roots).2.Nodup := by
  rw [evacuate_trace]
  exact List.Pairwise.map _ (fun left right ne eq =>
    ne (canonical_injective input _ (evacuate_invariant input roots).copying left right eq))
      (collect_trace_nodup input.heap roots)

theorem evacuate_trace_correct (input : CopyInput n) (roots : List (Fin n)) (object : Fin n) :
    canonical (evacuate input roots).1.copying object ∈ (evacuate input roots).2 ↔
      Reachable input.heap roots object := by
  rw [evacuate_trace, List.mem_map]
  constructor
  · rintro ⟨other, scanned, eq⟩
    have same := canonical_injective input _ (evacuate_invariant input roots).copying other object eq
    exact (collect_trace_correct input.heap roots object).mp (same ▸ scanned)
  · intro reachable
    exact ⟨object, (collect_trace_correct input.heap roots object).mpr reachable, rfl⟩

theorem evacuate_scans_once (input : CopyInput n) (roots : List (Fin n)) (object : Fin n)
    (reachable : Reachable input.heap roots object) :
    (evacuate input roots).2.count (canonical (evacuate input roots).1.copying object) = 1 := by
  have count := List.nodup_iff_count_eq_ite.mp (evacuate_trace_nodup input roots)
    (canonical (evacuate input roots).1.copying object)
  simpa [(evacuate_trace_correct input roots object).mpr reachable] using count

theorem evacuate_scan_bound (input : CopyInput n) (roots : List (Fin n)) :
    (evacuate input roots).2.length ≤ n := by
  simpa [evacuate_trace] using collect_scan_bound input.heap roots

end CloverGC.Copying
