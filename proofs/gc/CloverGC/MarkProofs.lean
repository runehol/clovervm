import CloverGC.Mark
import CloverGC.MarkedQueue

namespace CloverGC

/- Discovery lemmas describe both sides of the mark-before-enqueue operation.
   They apply even while scanning an object's children one by one. -/
theorem discover_marked (object x : Fin n) (state : MarkState n) :
    x ∈ (discover object state).marked ↔ x = object ∨ x ∈ state.marked := by
  simpa only [discover, MarkedQueue.enqueue, apply_ite] using
    MarkedQueue.marked_mem object x state.marked state.pending

theorem discover_pending (object x : Fin n) (state : MarkState n) :
    x ∈ (discover object state).pending ↔
      x ∈ state.pending ∨ (x = object ∧ object ∉ state.marked) := by
  simpa only [discover, MarkedQueue.enqueue, apply_ite] using
    MarkedQueue.pending_mem object x state.marked state.pending

theorem discoverMany_marked (objects : List (Fin n)) (state : MarkState n)
    (x : Fin n) :
    x ∈ (discoverMany objects state).marked ↔
      x ∈ state.marked ∨ x ∈ objects := by
  induction objects generalizing state with
  | nil => simp [discoverMany]
  | cons object rest ih =>
      simp only [discoverMany, ih, discover_marked, List.mem_cons]
      grind

theorem discoverMany_pending (objects : List (Fin n)) (state : MarkState n)
    (x : Fin n) :
    x ∈ (discoverMany objects state).pending ↔
      x ∈ state.pending ∨ (x ∈ objects ∧ x ∉ state.marked) := by
  induction objects generalizing state with
  | nil => simp [discoverMany]
  | cons object rest ih =>
      simp only [discoverMany, ih, discover_marked, discover_pending, List.mem_cons]
      grind

theorem discover_nodup (object : Fin n) (state : MarkState n)
    (marked : state.marked.Nodup) (pending : state.pending.Nodup)
    (included : ∀ x ∈ state.pending, x ∈ state.marked) :
    (discover object state).marked.Nodup ∧
      (discover object state).pending.Nodup := by
  simpa only [discover, MarkedQueue.enqueue, apply_ite] using
    MarkedQueue.nodup object state.marked state.pending marked pending included

theorem discoverMany_nodup (objects : List (Fin n)) (state : MarkState n)
    (marked : state.marked.Nodup) (pending : state.pending.Nodup)
    (included : ∀ x ∈ state.pending, x ∈ state.marked) :
    (discoverMany objects state).marked.Nodup ∧
      (discoverMany objects state).pending.Nodup := by
  induction objects generalizing state with
  | nil => exact ⟨marked, pending⟩
  | cons object rest ih =>
      obtain ⟨hm, hp⟩ := discover_nodup object state marked pending included
      apply ih (discover object state) hm hp
      simpa only [discover, MarkedQueue.enqueue, apply_ite] using
        MarkedQueue.included object state.marked state.pending included

/- `scanned` is proof-side execution history, never consulted by discovery. -/
structure Invariant (heap : Heap n) (roots scanned : List (Fin n))
    (state : MarkState n) : Prop where
  marked_nodup : state.marked.Nodup
  pending_nodup : state.pending.Nodup
  scanned_nodup : scanned.Nodup
  disjoint : ∀ x ∈ state.pending, x ∉ scanned
  accounted : ∀ x, x ∈ state.marked ↔ x ∈ state.pending ∨ x ∈ scanned
  sound : ∀ x ∈ state.marked, Reachable heap roots x
  closed : ∀ parent ∈ scanned, ∀ child ∈ heap.strongChildren parent,
    child ∈ state.marked

theorem seed_invariant (heap : Heap n) (roots : List (Fin n)) :
    Invariant heap roots [] (seed roots) := by
  have hm (x : Fin n) : x ∈ (seed roots).marked ↔ x ∈ roots := by
    simp [seed, discoverMany_marked, MarkState.empty]
  have hp (x : Fin n) : x ∈ (seed roots).pending ↔ x ∈ roots := by
    simp [seed, discoverMany_pending, MarkState.empty]
  obtain ⟨hnm, hnp⟩ := discoverMany_nodup roots (MarkState.empty : MarkState n)
    (by simp [MarkState.empty]) (by simp [MarkState.empty])
    (by simp [MarkState.empty])
  exact {
    marked_nodup := hnm
    pending_nodup := hnp
    scanned_nodup := by simp
    disjoint := by simp
    accounted := by intro x; simp [hm, hp]
    sound := by intro x hx; exact Reachable.root ((hm x).mp hx)
    closed := by simp
  }

theorem scan_preserves (heap : Heap n) (roots scanned : List (Fin n))
    (state : MarkState n) (object : Fin n) (rest : List (Fin n))
    (h : Invariant heap roots scanned state) (queue : state.pending = object :: rest) :
    Invariant heap roots (object :: scanned)
      (discoverMany (heap.strongChildren object) ⟨state.marked, rest⟩) := by
  let next := discoverMany (heap.strongChildren object) ⟨state.marked, rest⟩
  have hm (x : Fin n) : x ∈ next.marked ↔
      x ∈ state.marked ∨ x ∈ heap.strongChildren object :=
    discoverMany_marked _ _ _
  have hp (x : Fin n) : x ∈ next.pending ↔
      x ∈ rest ∨ (x ∈ heap.strongChildren object ∧ x ∉ state.marked) :=
    discoverMany_pending _ _ _
  have pending_nodup : (object :: rest).Nodup := queue ▸ h.pending_nodup
  have object_marked : object ∈ state.marked :=
    (h.accounted object).mpr (Or.inl (by simp [queue]))
  have object_reachable := h.sound object object_marked
  have included : ∀ x ∈ rest, x ∈ state.marked := by
    intro x hx
    exact (h.accounted x).mpr (Or.inl (by simp [queue, hx]))
  obtain ⟨hnm, hnp⟩ := discoverMany_nodup (heap.strongChildren object)
    (⟨state.marked, rest⟩ : MarkState n) h.marked_nodup
    (List.nodup_cons.mp pending_nodup).2 included
  have hdisjoint := h.disjoint
  have haccounted := h.accounted
  refine {
    marked_nodup := hnm
    pending_nodup := hnp
    scanned_nodup := ?_
    disjoint := ?_
    accounted := ?_
    sound := ?_
    closed := ?_
  }
  · exact List.nodup_cons.mpr ⟨h.disjoint object (by simp [queue]), h.scanned_nodup⟩
  · intro x hx
    rw [hp] at hx
    simp only [List.mem_cons]
    have no_object : object ∉ rest := (List.nodup_cons.mp pending_nodup).1
    grind
  · intro x
    rw [hm, hp]
    simp only [List.mem_cons]
    have old := h.accounted x
    rw [queue, List.mem_cons] at old
    grind
  · intro x hx
    rcases (hm x).mp hx with hx | hx
    · exact h.sound x hx
    · exact Reachable.edge object_reachable hx
  · intro parent hparent child hchild
    rcases List.mem_cons.mp hparent with rfl | hparent
    · exact (hm child).mpr (Or.inr hchild)
    · exact (hm child).mpr (Or.inl (h.closed parent hparent child hchild))

/-- The queue and scan history occupy disjoint portions of a finite heap. -/
theorem scheduled_bound {n : Nat} {heap : Heap n}
    {roots scanned : List (Fin n)} {state : MarkState n}
    (h : Invariant heap roots scanned state) :
    scanned.length + state.pending.length ≤ n := by
  have nodup : (scanned ++ state.pending).Nodup := by
    rw [List.nodup_append]
    exact ⟨h.scanned_nodup, h.pending_nodup, by
      intro x hx y hy eq
      subst y
      exact h.disjoint x hy hx⟩
  have bound := nodup.length_le_of_subset
    (fun x _ => List.mem_finRange x)
  simpa using bound

/-- Every bounded run maintains the invariant and records exactly its pops. -/
theorem drain_invariant (heap : Heap n) (roots scanned : List (Fin n))
    (fuel : Nat) (state : MarkState n) (h : Invariant heap roots scanned state) :
    Invariant heap roots ((drain heap fuel state).2.reverse ++ scanned)
      (drain heap fuel state).1 := by
  induction fuel generalizing state scanned with
  | zero => simpa [drain] using h
  | succ fuel ih =>
      cases queue : state.pending with
      | nil => simpa [drain, scanOne, queue] using h
      | cons object rest =>
          have next := scan_preserves heap roots scanned state object rest h queue
          have result := ih (object :: scanned) _ next
          simpa [drain, scanOne, queue, List.reverse_cons, List.append_assoc] using result

/-- No timeout assumption: a budget of n minus the scans already done suffices. -/
theorem drain_complete (heap : Heap n) (roots scanned : List (Fin n))
    (fuel : Nat) (state : MarkState n) (h : Invariant heap roots scanned state)
    (budget : n ≤ scanned.length + fuel) :
    (drain heap fuel state).1.pending = [] := by
  induction fuel generalizing state scanned with
  | zero =>
      have bound := scheduled_bound h
      have empty_length : state.pending.length = 0 := by omega
      simpa [drain] using List.eq_nil_of_length_eq_zero empty_length
  | succ fuel ih =>
      cases queue : state.pending with
      | nil => simp [drain, scanOne, queue]
      | cons object rest =>
          have next := scan_preserves heap roots scanned state object rest h queue
          have enough : n ≤ (object :: scanned).length + fuel := by
            simp only [List.length_cons]
            omega
          simpa [drain, scanOne, queue] using ih (object :: scanned) _ next enough

theorem drain_marked_mono (heap : Heap n) (fuel : Nat) (state : MarkState n)
    (x : Fin n) (marked : x ∈ state.marked) :
    x ∈ (drain heap fuel state).1.marked := by
  induction fuel generalizing state with
  | zero => exact marked
  | succ fuel ih =>
      cases queue : state.pending with
      | nil => simpa [drain, scanOne, queue] using marked
      | cons object rest =>
          have next : x ∈ (discoverMany (heap.strongChildren object)
              (⟨state.marked, rest⟩ : MarkState n)).marked :=
            (discoverMany_marked _ _ _).mpr (Or.inl marked)
          simpa [drain, scanOne, queue] using ih _ next

theorem collect_invariant (heap : Heap n) (roots : List (Fin n)) :
    Invariant heap roots (collect heap roots).2.reverse (collect heap roots).1 := by
  simpa [collect] using drain_invariant heap roots [] n (seed roots)
    (seed_invariant heap roots)

/-- Termination as useful closure, not merely exhaustion of a fuel counter. -/
theorem collect_complete (heap : Heap n) (roots : List (Fin n)) :
    (collect heap roots).1.pending = [] := by
  exact drain_complete heap roots [] n (seed roots) (seed_invariant heap roots)
    (by simp)

/-- Main specification: marking equals independently defined reachability. -/
theorem collect_correct (heap : Heap n) (roots : List (Fin n)) (x : Fin n) :
    x ∈ (collect heap roots).1.marked ↔ Reachable heap roots x := by
  have inv := collect_invariant heap roots
  have complete := collect_complete heap roots
  constructor
  · exact inv.sound x
  · intro reachable
    induction reachable with
    | root hx =>
        apply drain_marked_mono heap n (seed roots)
        exact (discoverMany_marked roots .empty _).mpr (Or.inr hx)
    | @edge parent child _ edge ih =>
        have scanned := (inv.accounted parent).mp ih
        simp only [complete, List.not_mem_nil, false_or] at scanned
        exact inv.closed parent scanned child edge

theorem collect_trace_nodup (heap : Heap n) (roots : List (Fin n)) :
    (collect heap roots).2.Nodup := by
  have inv := collect_invariant heap roots
  exact inv.scanned_nodup.perm (List.reverse_perm _)

theorem collect_trace_correct (heap : Heap n) (roots : List (Fin n)) (x : Fin n) :
    x ∈ (collect heap roots).2 ↔ Reachable heap roots x := by
  have inv := collect_invariant heap roots
  have accounted := inv.accounted x
  simp only [collect_complete, List.not_mem_nil, List.mem_reverse, false_or] at accounted
  exact accounted.symm.trans (collect_correct heap roots x)

/-- Every reachable object is scanned exactly once. -/
theorem collect_scans_once (heap : Heap n) (roots : List (Fin n)) (x : Fin n)
    (reachable : Reachable heap roots x) :
    (collect heap roots).2.count x = 1 := by
  have count := List.nodup_iff_count_eq_ite.mp (collect_trace_nodup heap roots) x
  simpa [(collect_trace_correct heap roots x).mpr reachable] using count

/-- Unreachable objects are not scanned, even if they point into the live graph. -/
theorem collect_skips_unreachable (heap : Heap n) (roots : List (Fin n)) (x : Fin n)
    (unreachable : ¬ Reachable heap roots x) :
    (collect heap roots).2.count x = 0 := by
  apply List.count_eq_zero_of_not_mem
  intro scanned
  exact unreachable ((collect_trace_correct heap roots x).mp scanned)

theorem collect_scan_bound (heap : Heap n) (roots : List (Fin n)) :
    (collect heap roots).2.length ≤ n := by
  have bound := scheduled_bound (collect_invariant heap roots)
  simpa [collect_complete] using bound

end CloverGC
