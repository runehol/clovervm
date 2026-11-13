import CloverGC.GenerationalProofs

namespace CloverGC.Generational

open Copying

def advance (input : CollectionInput n) (logical : MarkState n) (object : Fin n) : MarkState n :=
  if input.selected object then CloverGC.discover object logical else logical

structure Tracking (input : CollectionInput n) (state : CopyState n) (logical : MarkState n) : Prop where
  copying : CopyInvariant input.copyInput state
  schedule : Schedule input.copyInput state logical
  marked_selected : ∀ x ∈ logical.marked, input.selected x = true

/-- Logical pending inclusion follows from the physical queue invariant and
    the exact schedule correspondence. -/
theorem Tracking.included (track : Tracking input state logical) :
    ∀ object ∈ logical.pending, object ∈ logical.marked := by
  intro object pending
  have physical : canonical state object ∈ state.pending := by
    rw [track.schedule.pending]
    exact List.mem_map.mpr ⟨object, pending, rfl⟩
  have marked := track.copying.pending_marked _ physical
  rw [track.schedule.marked] at marked
  obtain ⟨other, member, same⟩ := List.mem_map.mp marked
  exact canonical_injective input.copyInput state track.copying other object same ▸ member

/-- Every allocated copy represents a logically marked, selected object. -/
theorem Tracking.selected (track : Tracking input state logical) : CopiesSelected input state := by
  intro index record present
  have represents : Represents input.copyInput state record.origin (.copied index) :=
    track.copying.ledger.copies_forwarded index record present
  exact track.marked_selected record.origin
    ((schedule_member input.copyInput state logical track.copying track.schedule record.origin (.copied index) represents).mp
      (track.copying.copies_marked index record present))

theorem tracking_empty (input : CollectionInput n) : Tracking input .empty .empty :=
  ⟨empty_invariant input.copyInput, schedule_empty input.copyInput, by simp [MarkState.empty]⟩

theorem tracking_discover (input : CollectionInput n) (state : CopyState n) (logical : MarkState n)
    (object : Fin n) (track : Tracking input state logical) :
    Tracking input (discover input state (.source object)).1 (advance input logical object) := by
  obtain ⟨hc, _⟩ := discover_invariant input state object track.copying track.selected
  refine ⟨hc, discover_schedule input state logical object track.copying track.schedule track.included, ?_⟩
  intro x member
  cases inside : input.selected object with
  | false => exact track.marked_selected x (by simpa [advance, inside] using member)
  | true =>
      have cases := (discover_marked object x logical).mp (by simpa [advance, inside] using member)
      rcases cases with rfl | old
      · exact inside
      · exact track.marked_selected x old

theorem advance_mono (input : CollectionInput n) (logical : MarkState n) (object target : Fin n)
    (member : target ∈ logical.marked) : target ∈ (advance input logical object).marked := by
  cases inside : input.selected object <;> simp [advance, inside, discover_marked, member]

theorem repair_tracking (input : CollectionInput n) (state : CopyState n) (logical : MarkState n)
    (objects : List (Fin n)) (track : Tracking input state logical) :
    Tracking input (repair input state (objects.map Reference.source)).1
      (discoverMany (objects.filter input.selected) logical) := by
  induction objects generalizing state logical with
  | nil => simpa [repair, discoverMany] using track
  | cons object rest ih =>
      have next := tracking_discover input state logical object track
      have tail := ih _ _ next
      cases inside : input.selected object <;>
        simpa [repair, List.filter, inside, advance, discoverMany] using tail

theorem repair_stable (input : CollectionInput n) (state : CopyState n) (logical : MarkState n)
    (objects : List (Fin n)) (track : Tracking input state logical) (target : Fin n)
    (known : input.selected target = true → target ∈ logical.marked) :
    canonical (repair input state (objects.map Reference.source)).1 target = canonical state target := by
  induction objects generalizing state logical with
  | nil => rfl
  | cons object rest ih =>
      have next := tracking_discover input state logical object track
      have tail := ih _ _ next (fun ht => advance_mono input logical object target (known ht))
      exact tail.trans (discover_canonical_stable input state logical object target
        track.copying track.selected track.schedule known)

theorem repair_outputs (input : CollectionInput n) (state : CopyState n) (logical : MarkState n)
    (objects : List (Fin n)) (track : Tracking input state logical) :
    (repair input state (objects.map Reference.source)).2 =
      objects.map (canonical (repair input state (objects.map Reference.source)).1) := by
  induction objects generalizing state logical with
  | nil => rfl
  | cons object rest ih =>
      let next := discover input state (.source object)
      let logicalNext := advance input logical object
      have hn : Tracking input next.1 logicalNext := tracking_discover input state logical object track
      have known : input.selected object = true → object ∈ logicalNext.marked := by
        intro inside
        simp [logicalNext, advance, inside, discover_marked]
      have stable := repair_stable input next.1 logicalNext rest hn object known
      have tail := ih next.1 logicalNext hn
      simp only [List.map_cons, repair]
      congr 1
      · exact (discover_result input state object track.copying track.selected).trans stable.symm

/-- A field is settled when every selected target has already been discovered.
    Unselected targets remain sources without requiring a mark. -/
def Closed (input : CollectionInput n) (logical : MarkState n) (owners : List (Fin n)) : Prop :=
  ∀ owner ∈ owners, ∀ child ∈ input.heap.data.graph.strongChildren owner,
    input.selected child = true → child ∈ logical.marked

def originalPlan (input : CollectionInput n) (copying : CopyState n)
    (rememberedDone scanned : List (Fin n)) (object : Fin n) : List (Reference n) :=
  if object ∈ rememberedDone ∨ (object ∈ scanned ∧ input.copyInput.movable object = false)
  then (input.heap.data.graph.strongChildren object).map (canonical copying)
  else rawFields input.copyInput object

def copyPlan (input : CollectionInput n) (copying : CopyState n)
    (scanned : List (Fin n)) (object : Fin n) : List (Reference n) :=
  if object ∈ scanned then (input.heap.data.graph.strongChildren object).map (canonical copying)
  else rawFields input.copyInput object

structure FieldsInvariant (input : CollectionInput n) (rememberedDone scanned : List (Fin n))
    (state : EvacuationState n) : Prop where
  originals : ∀ object, state.originalFields object = originalPlan input state.copying rememberedDone scanned object
  copies : ∀ (index : Nat) (record : CopyRecord n), state.copying.copies[index]? = some record →
    record.strongChildren = copyPlan input state.copying scanned record.origin

theorem fields_discover (input : CollectionInput n) (state : EvacuationState n)
    (logical : MarkState n) (done scanned : List (Fin n)) (object : Fin n)
    (track : Tracking input state.copying logical) (fields : FieldsInvariant input done scanned state)
    (closed : Closed input logical (done ++ scanned))
    (scannedMarked : ∀ o ∈ scanned, o ∈ logical.marked) :
    FieldsInvariant input done scanned
      { state with copying := (discover input state.copying (.source object)).1 } := by
  let next := (discover input state.copying (.source object)).1
  have stable (owner : Fin n) (mem : owner ∈ done ++ scanned) :
      (input.heap.data.graph.strongChildren owner).map (canonical next) =
      (input.heap.data.graph.strongChildren owner).map (canonical state.copying) :=
    List.map_congr_left fun child edge => discover_canonical_stable input state.copying logical object child
      track.copying track.selected track.schedule (closed owner mem child edge)
  have originals (owner : Fin n) : originalPlan input next done scanned owner =
      originalPlan input state.copying done scanned owner := by
    unfold originalPlan
    split
    next h => exact stable owner (by rcases h with h | ⟨h, _⟩ <;> simp [h])
    next => rfl
  have copies (owner : Fin n) : copyPlan input next scanned owner =
      copyPlan input state.copying scanned owner := by
    unfold copyPlan
    split
    next h => exact stable owner (by simp [h])
    next => rfl
  refine ⟨fun owner => (fields.originals owner).trans (originals owner).symm, ?_⟩
  intro index record get
  cases inside : input.selected object with
  | false =>
      have old : state.copying.copies[index]? = some record := by simpa [discover, inside] using get
      exact (fields.copies index record old).trans (copies record.origin).symm
  | true =>
      cases movable : input.copyInput.movable object with
      | false =>
          have old : state.copying.copies[index]? = some record := by
            simpa [discover, inside, discoverReference, movable] using get
          exact (fields.copies index record old).trans (copies record.origin).symm
      | true =>
          cases forward : state.copying.forwarding object with
          | some _ =>
              have old : state.copying.copies[index]? = some record := by
                simpa [discover, inside, discoverReference, movable, forward] using get
              exact (fields.copies index record old).trans (copies record.origin).symm
          | none =>
              have appended : (state.copying.copies ++ [input.copyInput.makeCopy object])[index]? = some record := by
                simpa [discover, inside, discoverReference, movable, forward, installCopy] using get
              rcases (append_record _ _ _ _).mp appended with old | ⟨rfl, rfl⟩
              · exact (fields.copies index record old).trans (copies record.origin).symm
              · have notScanned : object ∉ scanned := by
                  intro mem
                  have known := track.schedule.known object (scannedMarked object mem)
                  simp [Represents, canonical, forward, movable] at known
                simp [CopyInput.makeCopy, copyPlan, notScanned, rawFields]

theorem closed_mono (input : CollectionInput n) (before after : MarkState n) (owners : List (Fin n))
    (closed : Closed input before owners) (mono : ∀ o ∈ before.marked, o ∈ after.marked) :
    Closed input after owners := fun o ho c hc selected => mono c (closed o ho c hc selected)

theorem fields_repair (input : CollectionInput n) (state : EvacuationState n)
    (logical : MarkState n) (done scanned objects : List (Fin n))
    (track : Tracking input state.copying logical) (fields : FieldsInvariant input done scanned state)
    (closed : Closed input logical (done ++ scanned))
    (scannedMarked : ∀ o ∈ scanned, o ∈ logical.marked) :
    FieldsInvariant input done scanned
      { state with copying := (repair input state.copying (objects.map Reference.source)).1 } := by
  induction objects generalizing state logical with
  | nil => simpa [repair] using fields
  | cons object rest ih =>
      let next := { state with copying := (discover input state.copying (.source object)).1 }
      let logicalNext := advance input logical object
      have ht : Tracking input next.copying logicalNext := tracking_discover input state.copying logical object track
      have hf := fields_discover input state logical done scanned object track fields closed scannedMarked
      have hc := closed_mono input logical logicalNext _ closed (advance_mono input logical object)
      have hs : ∀ o ∈ scanned, o ∈ logicalNext.marked := fun o ho => advance_mono input logical object o (scannedMarked o ho)
      simpa [repair, next] using ih next logicalNext ht hf hc hs

theorem tracking_write (input : CollectionInput n) (state : EvacuationState n)
    (logical : MarkState n) (ref : Reference n) (values : List (Reference n))
    (track : Tracking input state.copying logical) :
    Tracking input (writeFields state ref values).copying logical :=
  ⟨write_preserves input.copyInput state ref values track.copying,
    write_schedule input.copyInput state ref values logical track.schedule, track.marked_selected⟩

theorem fields_write (input : CollectionInput n) (state : EvacuationState n)
    (done scanned : List (Fin n)) (parent : Fin n) (ref : Reference n)
    (h : CopyInvariant input.copyInput state.copying) (fields : FieldsInvariant input done scanned state)
    (rep : Represents input.copyInput state.copying parent ref) :
    FieldsInvariant input done (parent :: scanned)
      (writeFields state ref ((input.heap.data.graph.strongChildren parent).map (canonical state.copying))) := by
  have source_eq (object : Fin n) : ref = .source object ↔
      input.copyInput.movable object = false ∧ object = parent := by
    constructor
    · intro eq
      rw [eq] at rep
      exact ⟨by simpa [rep.1] using rep.2, rep.1⟩
    · rintro ⟨stationary, same⟩
      subst object
      exact unique_representation input.copyInput state.copying parent ref (.source parent) h rep ⟨rfl, stationary⟩
  constructor
  · intro object
    rw [write_original]
    simp only [source_eq, fields.originals]
    by_cases eq : object = parent
    · subst object
      by_cases stationary : input.copyInput.movable parent = false <;> simp [originalPlan, stationary, write_canonical]
    · simp [originalPlan, eq, write_canonical]
  · intro index record get
    rw [write_get, Option.map_eq_some_iff] at get
    obtain ⟨old, oldget, rfl⟩ := get
    have copied_eq : ref = .copied index ↔ old.origin = parent := by
      constructor
      · intro eq
        exact no_source_merging input.copyInput state.copying old.origin parent (.copied index) h
          (h.ledger.copies_forwarded index old oldget) (eq ▸ rep)
      · intro eq
        apply unique_representation input.copyInput state.copying parent ref (.copied index) h rep
        simpa [Represents, eq] using h.ledger.copies_forwarded index old oldget
    simp only [written_origin]
    by_cases eq : old.origin = parent
    · simp [writtenRecord, copied_eq.mpr eq, eq, copyPlan, write_canonical]
    · have absent : ref ≠ .copied index := fun equality => eq (copied_eq.mp equality)
      simpa [writtenRecord, absent, copyPlan, eq, write_canonical] using fields.copies index old oldget

theorem fields_remember (input : CollectionInput n) (state : EvacuationState n)
    (done : List (Fin n)) (owner : Fin n) (fields : FieldsInvariant input done [] state) :
    FieldsInvariant input (done ++ [owner]) []
      (writeFields state (.source owner) ((input.heap.data.graph.strongChildren owner).map (canonical state.copying))) := by
  constructor
  · intro object
    rw [write_original]
    by_cases same : owner = object
    · subst object
      simp [originalPlan, write_canonical]
    · have different : object ≠ owner := Ne.symm same
      simpa [originalPlan, same, different, write_canonical] using fields.originals object
  · intro index record get
    simpa [writeFields, copyPlan] using fields.copies index record get

end CloverGC.Generational
