import CloverGC.EvacuationFields

namespace CloverGC.Copying

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
  have seededFields := fields_repair input initial .empty [] roots (empty_invariant input)
    (schedule_empty input) (by simp [MarkState.empty]) initialFields
    (by simp) (by simp)
  refine {
    copying := repair_preserves input .empty roots (empty_invariant input)
    schedule := repair_schedule input .empty .empty roots (empty_invariant input)
      (schedule_empty input) (by simp [MarkState.empty])
    marking := seed_invariant input.heap roots
    fields := { originals := seededFields.originals, copies := seededFields.copies }
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
    ledger := {
      forwarding_valid := h.ledger.forwarding_valid
      copies_forwarded := h.ledger.copies_forwarded
      copies_metadata := h.ledger.copies_metadata
    }
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
    (rest : List (Fin n)) (invariant : EvacuationInvariant input roots scanned logical state)
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
    simp [invariant.schedule.pending, queue, reference]
  have objectMarked : object ∈ logical.marked :=
    (invariant.marking.accounted object).mpr (Or.inl (by simp [queue]))
  have notScanned : object ∉ scanned := invariant.marking.disjoint object (by simp [queue])
  have unscannedFields : liveFields state reference = rawFields input object := by
    rw [liveFields_plan input state scanned object reference invariant.copying invariant.fields
      (invariant.schedule.known object objectMarked)]
    simp [fieldPlan, notScanned]
  have poppedInvariant := pop_preserves input state.copying reference _ invariant.copying physicalQueue
  have poppedSchedule : Schedule input popped logicalPopped := {
    marked := invariant.schedule.marked
    pending := rfl
    known := invariant.schedule.known
  }
  have included : ∀ x ∈ logicalPopped.pending, x ∈ logicalPopped.marked := by
    intro x hx
    exact (invariant.marking.accounted x).mpr (Or.inl (by simp [queue, logicalPopped] at hx ⊢; exact Or.inr hx))
  have poppedFields : FieldsInvariant input scanned { state with copying := popped } :=
    ⟨invariant.fields.originals, invariant.fields.copies⟩
  have scannedMarked : ∀ x ∈ scanned, x ∈ logicalPopped.marked := by
    intro x hx
    exact (invariant.marking.accounted x).mpr (Or.inr hx)
  have preservedFields := fields_repair input { state with copying := popped } logicalPopped scanned
    (input.heap.strongChildren object) poppedInvariant poppedSchedule included poppedFields
    scannedMarked invariant.marking.closed
  have repairedInvariant := repair_preserves input popped (input.heap.strongChildren object) poppedInvariant
  have repairedSchedule := repair_schedule input popped logicalPopped (input.heap.strongChildren object)
    poppedInvariant poppedSchedule included
  have repairedFields := repair_outputs input popped logicalPopped (input.heap.strongChildren object)
    poppedInvariant poppedSchedule included
  have stable (x : Fin n) (hx : x ∈ logical.marked) : canonical repaired.1 x = canonical state.copying x :=
    repair_canonical_stable input popped (input.heap.strongChildren object) logicalPopped
      poppedInvariant poppedSchedule included x hx
  have newObjectMarked : object ∈ (discoverMany (input.heap.strongChildren object) logicalPopped).marked :=
    (discoverMany_marked _ _ _).mpr (Or.inl objectMarked)
  have scannedRepresentation : Represents input repaired.1 object reference := by
    have known := repairedSchedule.known object newObjectMarked
    rw [stable object objectMarked] at known
    exact known
  refine ⟨next, ?_, ?_, ?_⟩
  · simp [scanCopy, physicalQueue, unscannedFields, rawFields, next, prepared, repaired, popped, reference]
  · refine {
      copying := write_preserves input prepared reference repaired.2 repairedInvariant
      schedule := write_schedule input prepared reference repaired.2 _ repairedSchedule
      marking := scan_preserves input.heap roots scanned logical object rest invariant.marking queue
      fields := ?_
      roots_marked := ?_
      roots_repaired := ?_
    }
    · change FieldsInvariant input (object :: scanned) (writeFields prepared reference repaired.2)
      have repairedFields_eq : repaired.2 =
          (input.heap.strongChildren object).map (canonical repaired.1) := repairedFields
      rw [repairedFields_eq]
      exact write_fields input prepared scanned object reference repairedInvariant preservedFields scannedRepresentation
    · intro x hx
      exact (discoverMany_marked _ _ _).mpr (Or.inl (invariant.roots_marked x hx))
    · rw [write_roots, write_canonical]
      exact invariant.roots_repaired.trans
        (List.map_congr_left (fun x hx => (stable x (invariant.roots_marked x hx)).symm))
  · intro x hx
    exact (congrFun (write_canonical prepared reference repaired.2) x).trans (stable x hx)

/-- Simulation is proved for arbitrary fuel; completion will follow from the
    mark theorem, not from assuming the evacuation queue becomes empty. -/
theorem drain_evacuation (input : CopyInput n) (roots scanned : List (Fin n))
    (fuel : Nat) (logical : MarkState n) (state : EvacuationState n)
    (invariant : EvacuationInvariant input roots scanned logical state) :
    let logicalResult := drain input.heap fuel logical
    let copyResult := drainCopies input fuel state
    EvacuationInvariant input roots (logicalResult.2.reverse ++ scanned) logicalResult.1 copyResult.1 ∧
    copyResult.2 = logicalResult.2.map (canonical copyResult.1.copying) ∧
    ∀ x ∈ logical.marked, canonical copyResult.1.copying x = canonical state.copying x := by
  induction fuel generalizing scanned logical state with
  | zero =>
      exact ⟨by simpa [drain, drainCopies] using invariant, rfl, by intro x hx; rfl⟩
  | succ fuel inductionHypothesis =>
      cases queue : logical.pending with
      | nil =>
          have physical : state.copying.pending = [] := by simp [invariant.schedule.pending, queue]
          simpa [drain, scanOne, queue, drainCopies, scanCopy, physical] using
            (show EvacuationInvariant input roots scanned logical state ∧
              ([] : List (Reference n)) = [] ∧
              (∀ x ∈ logical.marked, canonical state.copying x = canonical state.copying x) from
                ⟨invariant, rfl, by intro x hx; rfl⟩)
      | cons object rest =>
          obtain ⟨next, step, nextInvariant, stepStable⟩ :=
            scan_evacuation input roots scanned logical state object rest invariant queue
          obtain ⟨finalInvariant, trace, tailStable⟩ :=
            inductionHypothesis (object :: scanned) _ next nextInvariant
          have oldMarked : object ∈ logical.marked :=
            (invariant.marking.accounted object).mpr (Or.inl (by simp [queue]))
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
