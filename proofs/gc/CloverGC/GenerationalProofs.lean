import CloverGC.Generational
import CloverGC.EvacuationProofs

namespace CloverGC.Generational

open Copying

theorem remembered_unselected (input : CollectionInput n) (owner : Fin n)
    (member : owner ∈ input.remembered) : input.selected owner = false := by
  cases mode : input.collection with
  | full => simp [CollectionInput.remembered, mode] at member
  | young =>
      simpa [CollectionInput.selected, mode, RawGenerationalHeap.isOld] using
        input.heap.valid.remembered_old owner
          (by simpa [CollectionInput.remembered, mode] using member)

theorem remembered_nodup (input : CollectionInput n) : input.remembered.Nodup := by
  cases mode : input.collection <;> simp [CollectionInput.remembered, mode,
    input.heap.valid.remembered_unique]

theorem coverage (input : CollectionInput n) (owner child : Fin n)
    (outside : input.selected owner = false)
    (edge : child ∈ input.heap.data.graph.strongChildren owner)
    (inside : input.selected child = true) : owner ∈ input.remembered := by
  cases mode : input.collection with
  | full => simp [CollectionInput.selected, mode] at outside
  | young =>
      simpa [CollectionInput.remembered, mode] using
        input.heap.valid.old_to_young_covered owner child
          (by simpa [RawGenerationalHeap.isOld, CollectionInput.selected, mode] using outside) edge
          (by simpa [CollectionInput.selected, mode] using inside)

def CopiesSelected (input : CollectionInput n) (state : CopyState n) : Prop :=
  ∀ (index : Nat) (record : CopyRecord n), state.copies[index]? = some record → input.selected record.origin = true

theorem unselected_unforwarded (input : CollectionInput n) (state : CopyState n)
    (h : CopyInvariant input.copyInput state) (selected : CopiesSelected input state)
    (object : Fin n) (outside : input.selected object = false) : state.forwarding object = none := by
  cases forward : state.forwarding object with
  | none => rfl
  | some index =>
      obtain ⟨record, get, origin⟩ := h.ledger.forwarding_valid object index forward
      have inside := selected index record get
      simp [origin, outside] at inside

theorem unselected_canonical (input : CollectionInput n) (state : CopyState n)
    (h : CopyInvariant input.copyInput state) (selected : CopiesSelected input state)
    (object : Fin n) (outside : input.selected object = false) :
    canonical state object = .source object := by
  simp [canonical, unselected_unforwarded input state h selected object outside]

theorem discover_invariant (input : CollectionInput n) (state : CopyState n) (object : Fin n)
    (h : CopyInvariant input.copyInput state) (selected : CopiesSelected input state) :
    CopyInvariant input.copyInput (discover input state (.source object)).1 ∧
    CopiesSelected input (discover input state (.source object)).1 := by
  cases inside : input.selected object with
  | false => simpa [discover, inside] using And.intro h selected
  | true =>
      refine ⟨?_, ?_⟩
      · simpa [discover, inside] using discovery_preserves input.copyInput state (.source object) h trivial
      · intro index record get
        cases movable : input.copyInput.movable object with
        | false => exact selected index record (by simpa [discover, inside, discoverReference, movable] using get)
        | true =>
            cases forward : state.forwarding object with
            | some _ => exact selected index record (by simpa [discover, inside, discoverReference, movable, forward] using get)
            | none =>
                have appended : (state.copies ++ [input.copyInput.makeCopy object])[index]? = some record := by
                  simpa [discover, inside, discoverReference, movable, forward, installCopy] using get
                rcases (append_record _ _ _ _).mp appended with old | ⟨rfl, rfl⟩
                · exact selected index record old
                · exact inside

theorem discover_schedule (input : CollectionInput n) (state : CopyState n)
    (logical : MarkState n) (object : Fin n) (h : CopyInvariant input.copyInput state)
    (s : Schedule input.copyInput state logical)
    (included : ∀ x ∈ logical.pending, x ∈ logical.marked) :
    Schedule input.copyInput (discover input state (.source object)).1
      (if input.selected object then CloverGC.discover object logical else logical) := by
  cases inside : input.selected object with
  | false => simpa [discover, inside] using s
  | true => simpa [discover, inside] using discovery_schedule input.copyInput state logical object h s included

theorem discover_canonical_stable (input : CollectionInput n) (state : CopyState n)
    (logical : MarkState n) (object target : Fin n) (h : CopyInvariant input.copyInput state)
    (selected : CopiesSelected input state) (s : Schedule input.copyInput state logical)
    (known : input.selected target = true → target ∈ logical.marked) :
    canonical (discover input state (.source object)).1 target = canonical state target := by
  cases inside : input.selected target with
  | false =>
      obtain ⟨hn, sn⟩ := discover_invariant input state object h selected
      rw [unselected_canonical input _ hn sn target inside, unselected_canonical input _ h selected target inside]
  | true =>
      cases incoming : input.selected object with
      | false => simp [discover, incoming]
      | true => simpa [discover, incoming] using
          discovery_canonical_stable input.copyInput state logical (.source object) h s trivial target (known inside)

theorem discover_result (input : CollectionInput n) (state : CopyState n) (object : Fin n)
    (h : CopyInvariant input.copyInput state) (selected : CopiesSelected input state) :
    (discover input state (.source object)).2 = canonical (discover input state (.source object)).1 object := by
  cases inside : input.selected object with
  | false => simp [discover, inside, unselected_canonical input state h selected object inside]
  | true =>
      exact (represents_canonical input.copyInput _ object _
        (discover_invariant input state object h selected).1
        (by simpa [discover, inside] using discovery_source_represents input.copyInput state object)).symm

/-- Keep the existing full collector as a proved specialization. -/
theorem full_discover (input : CollectionInput n) (full : input.collection = .full)
    (state : CopyState n) (ref : Reference n) :
    discover input state ref = discoverReference input.copyInput state ref := by
  cases ref <;> simp [discover, CollectionInput.selected, full, discoverReference]

theorem full_repair (input : CollectionInput n) (full : input.collection = .full)
    (state : CopyState n) (refs : List (Reference n)) :
    repair input state refs = repairReferences input.copyInput state refs := by
  induction refs generalizing state with
  | nil => rfl
  | cons ref refs ih => simp [repair, repairReferences, full_discover input full, ih]

theorem full_scan (input : CollectionInput n) (full : input.collection = .full)
    (state : EvacuationState n) : scan input state = scanCopy input.copyInput state := by
  cases pending : state.copying.pending <;> simp [scan, scanCopy, pending, full_repair input full]

theorem full_drain (input : CollectionInput n) (full : input.collection = .full)
    (fuel : Nat) (state : EvacuationState n) :
    drain input fuel state = drainCopies input.copyInput fuel state := by
  induction fuel generalizing state with
  | zero => rfl
  | succ fuel ih =>
      simp only [drain, full_scan input full, drainCopies]
      cases scanCopy input.copyInput state <;> simp [ih]

theorem full_evacuate (input : CollectionInput n) (full : input.collection = .full) :
    evacuate input = Copying.evacuate input.copyInput input.roots := by
  simp [evacuate, start, CollectionInput.remembered, full, remember,
    seedRoots, Copying.seedRoots, full_repair input full, full_drain input full, Copying.evacuate]

end CloverGC.Generational
