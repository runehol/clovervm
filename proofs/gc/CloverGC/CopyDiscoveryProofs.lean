import CloverGC.CopyDiscovery
import CloverGC.MarkedQueue

namespace CloverGC.Copying

theorem empty_invariant (input : CopyInput n) : CopyInvariant input .empty := by
  exact {
    ledger := {
      forwarding_valid := by simp [CopyState.empty]
      copies_forwarded := by simp [CopyState.empty]
      copies_metadata := by simp [CopyState.empty]
    }
    marked_canonical := by simp [CopyState.empty]
    copies_marked := by simp [CopyState.empty]
    marked_nodup := by simp [CopyState.empty]
    pending_nodup := by simp [CopyState.empty]
    pending_marked := by simp [CopyState.empty]
  }

theorem enqueue_marked (reference x : Reference n) (state : CopyState n) :
    x ∈ (enqueueOnce reference state).marked ↔ x = reference ∨ x ∈ state.marked := by
  simpa only [enqueueOnce, MarkedQueue.enqueue, apply_ite] using
    MarkedQueue.marked_mem reference x state.marked state.pending

theorem enqueue_pending (reference x : Reference n) (state : CopyState n) :
    x ∈ (enqueueOnce reference state).pending ↔
      x ∈ state.pending ∨ (x = reference ∧ reference ∉ state.marked) := by
  simpa only [enqueueOnce, MarkedQueue.enqueue, apply_ite] using
    MarkedQueue.pending_mem reference x state.marked state.pending

@[simp] theorem enqueue_copies (reference : Reference n) (state : CopyState n) :
    (enqueueOnce reference state).copies = state.copies := by
  simp only [enqueueOnce]
  split <;> rfl

@[simp] theorem enqueue_forwarding (reference : Reference n) (state : CopyState n) :
    (enqueueOnce reference state).forwarding = state.forwarding := by
  simp only [enqueueOnce]
  split <;> rfl

theorem enqueue_nodup (reference : Reference n) (state : CopyState n)
    (marked : state.marked.Nodup) (pending : state.pending.Nodup)
    (included : ∀ x ∈ state.pending, x ∈ state.marked) :
    (enqueueOnce reference state).marked.Nodup ∧
      (enqueueOnce reference state).pending.Nodup := by
  simpa only [enqueueOnce, MarkedQueue.enqueue, apply_ite] using
    MarkedQueue.nodup reference state.marked state.pending marked pending included

theorem enqueue_included (reference : Reference n) (state : CopyState n)
    (included : ∀ x ∈ state.pending, x ∈ state.marked) :
    ∀ x ∈ (enqueueOnce reference state).pending, x ∈ (enqueueOnce reference state).marked := by
  simpa only [enqueueOnce, MarkedQueue.enqueue, apply_ite] using
    MarkedQueue.included reference state.marked state.pending included

theorem enqueue_preserves (input : CopyInput n) (state : CopyState n)
    (reference : Reference n) (h : CopyInvariant input state)
    (canonical : Canonical input state reference) :
    CopyInvariant input (enqueueOnce reference state) := by
  obtain ⟨hm, hp⟩ := enqueue_nodup reference state
    h.marked_nodup h.pending_nodup h.pending_marked
  refine {
    ledger := ?_
    marked_canonical := ?_
    copies_marked := ?_
    marked_nodup := hm
    pending_nodup := hp
    pending_marked := ?_
  }
  · refine { forwarding_valid := ?_, copies_forwarded := ?_, copies_metadata := ?_ }
    · simpa using h.ledger.forwarding_valid
    · simpa using h.ledger.copies_forwarded
    · simpa using h.ledger.copies_metadata
  · intro x hx
    have old : Canonical input state x := by
      rcases (enqueue_marked reference x state).mp hx with rfl | hx
      · exact canonical
      · exact h.marked_canonical x hx
    cases x <;> simpa [Canonical] using old
  · intro index record hr
    rw [enqueue_copies] at hr
    exact (enqueue_marked reference (.copied index) state).mpr
      (Or.inr (h.copies_marked index record hr))
  · exact enqueue_included reference state h.pending_marked

/-- Appending allocates exactly one fresh index and preserves every old slot. -/
theorem append_record (records : List (CopyRecord n)) (fresh record : CopyRecord n)
    (index : Nat) :
    (records ++ [fresh])[index]? = some record ↔
      records[index]? = some record ∨ (index = records.length ∧ record = fresh) := by
  by_cases before : index < records.length
  · have ne : index ≠ records.length := by omega
    simp [List.getElem?_append, before, ne]
  · have offset : index - records.length = 0 ↔ index = records.length := by omega
    simp [List.getElem?_append, before, List.getElem?_singleton, offset, eq_comm]

theorem install_ledger (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (h : Ledger input state)
    (movable : input.movable object = true) (unforwarded : state.forwarding object = none) :
    Ledger input (installCopy input state object) := by
  refine { forwarding_valid := ?_, copies_forwarded := ?_, copies_metadata := ?_ }
  · intro other index forward
    by_cases same : other = object
    · subst other
      have eq : state.copies.length = index := by simpa [installCopy] using forward
      subst index
      exact ⟨input.makeCopy object,
        (append_record _ _ _ _).mpr (Or.inr ⟨rfl, rfl⟩), rfl⟩
    · have old : state.forwarding other = some index := by
        simpa [installCopy, same] using forward
      obtain ⟨record, get, origin⟩ := h.forwarding_valid other index old
      exact ⟨record, (append_record _ _ _ _).mpr (Or.inl get), origin⟩
  · intro index record get
    rcases (append_record _ _ _ _).mp get with old | ⟨rfl, rfl⟩
    · have forward := h.copies_forwarded index record old
      have ne : record.origin ≠ object := by
        intro eq
        rw [eq, unforwarded] at forward
        contradiction
      simpa [installCopy, ne] using forward
    · simp [installCopy, CopyInput.makeCopy]
  · intro index record get
    rcases (append_record _ _ _ _).mp get with old | ⟨rfl, rfl⟩
    · exact h.copies_metadata index record old
    · exact ⟨movable, rfl⟩

theorem install_canonical (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (reference : Reference n) (h : Canonical input state reference) :
    Canonical input (installCopy input state object) reference := by
  cases reference with
  | source other => exact h
  | copied index =>
      obtain ⟨record, get⟩ := h
      exact ⟨record, (append_record _ _ _ _).mpr (Or.inl get)⟩

/-- Allocation and forwarding publication are followed by marking before the
    caller can observe the returned copy. No intermediate invariant is assumed. -/
theorem fresh_copy_preserves (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (h : CopyInvariant input state)
    (movable : input.movable object = true) (unforwarded : state.forwarding object = none) :
    CopyInvariant input
      (enqueueOnce (.copied state.copies.length) (installCopy input state object)) := by
  let reference : Reference n := .copied state.copies.length
  let installed := installCopy input state object
  have ledger := install_ledger input state object h.ledger movable unforwarded
  have canonical : Canonical input installed reference :=
    ⟨input.makeCopy object, (append_record _ _ _ _).mpr (Or.inr ⟨rfl, rfl⟩)⟩
  obtain ⟨hm, hp⟩ := enqueue_nodup reference installed
    h.marked_nodup h.pending_nodup h.pending_marked
  refine {
    ledger := ?_
    marked_canonical := ?_
    copies_marked := ?_
    marked_nodup := hm
    pending_nodup := hp
    pending_marked := ?_
  }
  · refine { forwarding_valid := ?_, copies_forwarded := ?_, copies_metadata := ?_ }
    · simpa using ledger.forwarding_valid
    · simpa using ledger.copies_forwarded
    · simpa using ledger.copies_metadata
  · intro x hx
    have canonical_x : Canonical input installed x := by
      rcases (enqueue_marked reference x installed).mp hx with rfl | hx
      · exact canonical
      · exact install_canonical input state object x (h.marked_canonical x hx)
    cases x <;> simpa [Canonical] using canonical_x
  · intro index record get
    rw [enqueue_copies] at get
    apply (enqueue_marked reference (.copied index) installed).mpr
    rcases (append_record _ _ _ _).mp get with old | ⟨rfl, _⟩
    · exact Or.inr (h.copies_marked index record old)
    · exact Or.inl rfl
  · exact enqueue_included reference installed h.pending_marked

theorem discovery_preserves (input : CopyInput n) (state : CopyState n)
    (reference : Reference n) (h : CopyInvariant input state)
    (_valid : ValidReference state reference) :
    CopyInvariant input (discoverReference input state reference).1 := by
  cases reference with
  | copied index => exact h
  | source object =>
      cases movable : input.movable object with
      | false =>
          simpa [discoverReference, movable] using
            enqueue_preserves input state (.source object) h movable
      | true =>
          cases forward : state.forwarding object with
          | none =>
              simpa [discoverReference, movable, forward] using
                fresh_copy_preserves input state object h movable forward
          | some index =>
              obtain ⟨record, get, _⟩ := h.ledger.forwarding_valid object index forward
              simpa [discoverReference, movable, forward] using
                enqueue_preserves input state (.copied index) h ⟨record, get⟩

/-- A false mobility policy cannot have an allocated forwarding destination. -/
theorem nonmoving_unforwarded (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (h : CopyInvariant input state)
    (nonmoving : input.movable object = false) : state.forwarding object = none := by
  cases forward : state.forwarding object with
  | none => rfl
  | some index =>
      obtain ⟨record, get, origin⟩ := h.ledger.forwarding_valid object index forward
      have movable := (h.ledger.copies_metadata index record get).1
      rw [origin, nonmoving] at movable
      contradiction

theorem discovery_source_represents (input : CopyInput n) (state : CopyState n)
    (object : Fin n) :
    Represents input (discoverReference input state (.source object)).1 object
      (discoverReference input state (.source object)).2 := by
  cases movable : input.movable object with
  | false => simp [discoverReference, movable, Represents]
  | true =>
      cases forward : state.forwarding object <;>
        simp [discoverReference, movable, forward, Represents, installCopy]

/-- Even before the caller stores the repaired reference, it is already marked. -/
theorem discovery_returned_marked (input : CopyInput n) (state : CopyState n)
    (reference : Reference n) (h : CopyInvariant input state)
    (valid : ValidReference state reference) :
    (discoverReference input state reference).2 ∈
      (discoverReference input state reference).1.marked := by
  cases reference with
  | copied index =>
      obtain ⟨record, get⟩ := valid
      exact h.copies_marked index record get
  | source object =>
      cases movable : input.movable object with
      | false => simp [discoverReference, movable, enqueue_marked]
      | true =>
          cases forward : state.forwarding object <;>
            simp [discoverReference, movable, forward, enqueue_marked]

theorem discovery_returned_canonical (input : CopyInput n) (state : CopyState n)
    (reference : Reference n) (h : CopyInvariant input state)
    (valid : ValidReference state reference) :
    Canonical input (discoverReference input state reference).1
      (discoverReference input state reference).2 := by
  exact (discovery_preserves input state reference h valid).marked_canonical _
    (discovery_returned_marked input state reference h valid)

/-- Allocation is genuinely fresh and copies source edges without following them. -/
theorem discovery_fresh_copy (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (movable : input.movable object = true)
    (unforwarded : state.forwarding object = none) :
    let result := discoverReference input state (.source object)
    result.2 = .copied state.copies.length ∧
    result.1.copies = state.copies ++ [input.makeCopy object] ∧
    result.1.forwarding object = some state.copies.length ∧
    state.copies[state.copies.length]? = none := by
  simp [discoverReference, movable, unforwarded, installCopy]

theorem discovery_nonmoving (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (nonmoving : input.movable object = false) :
    let result := discoverReference input state (.source object)
    result.2 = .source object ∧ result.1.copies = state.copies ∧
      result.1.forwarding = state.forwarding := by
  simp [discoverReference, nonmoving]

/-- Discovery never overwrites any previously allocated destination record. -/
theorem discovery_preserves_copy (input : CopyInput n) (state : CopyState n)
    (reference : Reference n) (index : Nat) (record : CopyRecord n)
    (get : state.copies[index]? = some record) :
    (discoverReference input state reference).1.copies[index]? = some record := by
  cases reference with
  | copied _ => exact get
  | source object =>
      cases movable : input.movable object with
      | false => simpa [discoverReference, movable] using get
      | true =>
          cases forward : state.forwarding object with
          | some _ => simpa [discoverReference, movable, forward] using get
          | none =>
              simpa [discoverReference, movable, forward, installCopy] using
                (append_record state.copies (input.makeCopy object) record index).mpr
                  (Or.inl get)

/-- Once a source has a destination, later discoveries cannot change it. -/
theorem discovery_preserves_forwarding (input : CopyInput n) (state : CopyState n)
    (reference : Reference n) (object : Fin n) (index : Nat)
    (forward : state.forwarding object = some index) :
    (discoverReference input state reference).1.forwarding object = some index := by
  cases reference with
  | copied _ => exact forward
  | source other =>
      cases movable : input.movable other with
      | false => simpa [discoverReference, movable] using forward
      | true =>
          cases other_forward : state.forwarding other with
          | some _ => simpa [discoverReference, movable, other_forward] using forward
          | none =>
              have ne : object ≠ other := by
                intro eq
                rw [eq, other_forward] at forward
                contradiction
              simpa [discoverReference, movable, other_forward, installCopy, ne] using forward

/-- One physical reference cannot represent two different logical sources. -/
theorem no_source_merging (input : CopyInput n) (state : CopyState n)
    (left right : Fin n) (reference : Reference n) (h : CopyInvariant input state)
    (hl : Represents input state left reference)
    (hr : Represents input state right reference) : left = right := by
  cases reference with
  | source object => exact hl.1.symm.trans hr.1
  | copied index =>
      obtain ⟨left_record, left_get, left_origin⟩ := h.ledger.forwarding_valid left index hl
      obtain ⟨right_record, right_get, right_origin⟩ := h.ledger.forwarding_valid right index hr
      have eq : left_record = right_record := Option.some.inj (left_get.symm.trans right_get)
      exact left_origin.symm.trans ((congrArg CopyRecord.origin eq).trans right_origin)

/-- A logical source has at most one canonical representation in a valid state. -/
theorem unique_representation (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (left right : Reference n) (h : CopyInvariant input state)
    (hl : Represents input state object left)
    (hr : Represents input state object right) : left = right := by
  cases left with
  | source left =>
      cases right with
      | source right => exact congrArg Reference.source (hl.1.trans hr.1.symm)
      | copied index =>
          have empty := nonmoving_unforwarded input state object h hl.2
          simp only [Represents, empty] at hr
          contradiction
  | copied left =>
      cases right with
      | source right =>
          have empty := nonmoving_unforwarded input state object h hr.2
          simp only [Represents, empty] at hl
          contradiction
      | copied right => exact congrArg Reference.copied (Option.some.inj (hl.symm.trans hr))

theorem canonical_rediscovery (input : CopyInput n) (state : CopyState n)
    (reference : Reference n) (canonical : Canonical input state reference)
    (marked : reference ∈ state.marked) :
    discoverReference input state reference = (state, reference) := by
  cases reference with
  | copied _ => rfl
  | source object =>
      change input.movable object = false at canonical
      simp [discoverReference, canonical, enqueueOnce, marked]

theorem represented_source_rediscovery (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (reference : Reference n) (h : CopyInvariant input state)
    (represents : Represents input state object reference)
    (marked : reference ∈ state.marked) :
    discoverReference input state (.source object) = (state, reference) := by
  cases reference with
  | source other =>
      obtain ⟨rfl, nonmoving⟩ := represents
      simp [discoverReference, nonmoving, enqueueOnce, marked]
  | copied index =>
      change state.forwarding object = some index at represents
      obtain ⟨record, get, origin⟩ := h.ledger.forwarding_valid object index represents
      have movable := (h.ledger.copies_metadata index record get).1
      rw [origin] at movable
      simp [discoverReference, movable, represents, enqueueOnce, marked]

/-- Repeating the original encounter allocates nothing and enqueues nothing. -/
theorem source_rediscovery (input : CopyInput n) (state : CopyState n)
    (object : Fin n) (h : CopyInvariant input state) :
    let result := discoverReference input state (.source object)
    discoverReference input result.1 (.source object) = result := by
  exact represented_source_rediscovery input _ object _
    (discovery_preserves input state (.source object) h trivial)
    (discovery_source_represents input state object)
    (discovery_returned_marked input state (.source object) h trivial)

/-- Rediscovering the repaired reference is also a complete no-op. -/
theorem returned_rediscovery (input : CopyInput n) (state : CopyState n)
    (reference : Reference n) (h : CopyInvariant input state)
    (valid : ValidReference state reference) :
    let result := discoverReference input state reference
    discoverReference input result.1 result.2 = result := by
  exact canonical_rediscovery input _ _
    (discovery_returned_canonical input state reference h valid)
    (discovery_returned_marked input state reference h valid)

end CloverGC.Copying
