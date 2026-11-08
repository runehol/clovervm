import CloverGC.GenerationalSchedule

namespace CloverGC.Generational

open Copying

def Live (input : CollectionInput n) (object : Fin n) : Prop :=
  Reachable input.selectedGraph input.logicalRoots object
def Retained (input : CollectionInput n) (object : Fin n) : Prop :=
  input.selected object = false ∨ Live input object

def Safe (input : CollectionInput n) (state : CopyState n) : Reference n → Prop
  | .source object => input.selected object = false ∨ input.copyInput.movable object = false
  | .copied index => ∃ record, state.copies[index]? = some record

theorem evacuate_reachable (input : CollectionInput n) (object : Fin n) :
    canonical (evacuate input).1.copying object ∈ (evacuate input).1.copying.marked ↔ Live input object := by
  have inv := evacuate_invariant input
  rw [inv.tracking.schedule.marked, List.mem_map]
  constructor
  · rintro ⟨other, member, eq⟩
    have same := canonical_injective input.copyInput _ inv.tracking.copying other object eq
    exact (collect_correct input.selectedGraph input.logicalRoots object).mp (same ▸ member)
  · intro live
    exact ⟨object, (collect_correct input.selectedGraph input.logicalRoots object).mpr live, rfl⟩

theorem live_selected (input : CollectionInput n) (object : Fin n) (live : Live input object) :
    input.selected object = true :=
  (evacuate_invariant input).tracking.marked_selected object
    ((collect_correct input.selectedGraph input.logicalRoots object).mpr live)

theorem evacuate_unselected (input : CollectionInput n) (object : Fin n)
    (outside : input.selected object = false) :
    (evacuate input).1.copying.forwarding object = none ∧
    Reference.source object ∉ (evacuate input).1.copying.marked := by
  have inv := evacuate_invariant input
  have forward := unselected_unforwarded input _ inv.tracking.copying inv.tracking.selected object outside
  refine ⟨forward, ?_⟩
  intro marked
  have canonicalMarked : canonical (evacuate input).1.copying object ∈ (evacuate input).1.copying.marked := by
    simpa [canonical, forward] using marked
  have inside := live_selected input object ((evacuate_reachable input object).mp canonicalMarked)
  simp [outside] at inside

theorem evacuate_forwarded_iff (input : CollectionInput n) (object : Fin n) :
    (∃ index, (evacuate input).1.copying.forwarding object = some index) ↔
      Live input object ∧ input.copyInput.movable object = true := by
  have inv := evacuate_invariant input
  constructor
  · rintro ⟨index, forward⟩
    obtain ⟨record, get, origin⟩ := inv.tracking.copying.ledger.forwarding_valid object index forward
    have marked := inv.tracking.copying.copies_marked index record get
    have live := (evacuate_reachable input object).mp (by simpa [canonical, forward] using marked)
    exact ⟨live, by simpa [origin] using (inv.tracking.copying.ledger.copies_metadata index record get).1⟩
  · rintro ⟨live, movable⟩
    have known := inv.tracking.schedule.known object
      ((collect_correct input.selectedGraph input.logicalRoots object).mpr live)
    cases forward : (evacuate input).1.copying.forwarding object with
    | none => simp [Represents, canonical, forward, movable] at known
    | some index => exact ⟨index, rfl⟩

theorem retained_child (input : CollectionInput n) (owner child : Fin n)
    (retained : Retained input owner) (edge : child ∈ input.heap.data.graph.strongChildren owner) :
    Retained input child := by
  cases inside : input.selected child with
  | false => exact Or.inl inside
  | true =>
      refine Or.inr ?_
      rcases retained with outside | live
      · have remembered := coverage input owner child outside edge inside
        apply Reachable.root
        apply List.mem_filter.mpr
        refine ⟨List.mem_append.mpr (Or.inr ?_), inside⟩
        exact List.mem_flatMap.mpr ⟨owner, remembered, edge⟩
      · exact Reachable.edge live (List.mem_filter.mpr ⟨edge, inside⟩)

theorem root_retained (input : CollectionInput n) (object : Fin n) (root : object ∈ input.roots) :
    Retained input object := by
  cases inside : input.selected object with
  | false => exact Or.inl inside
  | true => exact Or.inr (Reachable.root (List.mem_filter.mpr ⟨List.mem_append.mpr (Or.inl root), inside⟩))

theorem canonical_safe (input : CollectionInput n) (object : Fin n) (retained : Retained input object) :
    Safe input (evacuate input).1.copying (canonical (evacuate input).1.copying object) := by
  have inv := evacuate_invariant input
  rcases retained with outside | live
  · rw [unselected_canonical input _ inv.tracking.copying inv.tracking.selected object outside]
    exact Or.inl outside
  · have safe := inv.tracking.copying.marked_canonical _ ((evacuate_reachable input object).mpr live)
    cases eq : canonical (evacuate input).1.copying object with
    | source other => exact Or.inr (by simpa [Canonical, eq] using safe)
    | copied index => simpa [Canonical, eq, Safe] using safe

theorem evacuate_roots (input : CollectionInput n) :
    (evacuate input).1.roots = input.roots.map (canonical (evacuate input).1.copying) :=
  (evacuate_invariant input).roots_repaired

theorem unselected_fields (input : CollectionInput n) (owner : Fin n)
    (outside : input.selected owner = false) :
    (evacuate input).1.originalFields owner =
      (input.heap.data.graph.strongChildren owner).map (canonical (evacuate input).1.copying) := by
  have inv := evacuate_invariant input
  rw [inv.fields.originals]
  unfold originalPlan
  split
  next => rfl
  next notRepaired =>
    apply List.map_congr_left
    intro child edge
    have childOutside : input.selected child = false := by
      cases selected : input.selected child with
      | false => rfl
      | true => exact False.elim (notRepaired (Or.inl (coverage input owner child outside edge selected)))
    exact (unselected_canonical input _ inv.tracking.copying inv.tracking.selected child childOutside).symm

theorem evacuate_children (input : CollectionInput n) (owner : Fin n) (retained : Retained input owner) :
    liveFields (evacuate input).1 (canonical (evacuate input).1.copying owner) =
      (input.heap.data.graph.strongChildren owner).map (canonical (evacuate input).1.copying) := by
  have inv := evacuate_invariant input
  rcases retained with outside | live
  · rw [unselected_canonical input _ inv.tracking.copying inv.tracking.selected owner outside]
    exact unselected_fields input owner outside
  · have scanned : owner ∈ (collect input.selectedGraph input.logicalRoots).2.reverse :=
      List.mem_reverse.mpr ((collect_trace_correct input.selectedGraph input.logicalRoots owner).mpr live)
    have known := inv.tracking.schedule.known owner ((collect_correct input.selectedGraph input.logicalRoots owner).mpr live)
    cases eq : canonical (evacuate input).1.copying owner with
    | source other =>
        rw [eq] at known
        obtain ⟨same, stationary⟩ := known
        subst other
        simpa [liveFields, eq, originalPlan, scanned, stationary] using inv.fields.originals owner
    | copied index =>
        rw [eq] at known
        obtain ⟨record, get, origin⟩ := inv.tracking.copying.ledger.forwarding_valid owner index known
        simpa [liveFields, eq, get, copyPlan, origin, scanned] using inv.fields.copies index record get

theorem evacuate_disposal_safe (input : CollectionInput n) :
    (∀ ref ∈ (evacuate input).1.roots, Safe input (evacuate input).1.copying ref) ∧
    (∀ owner, Retained input owner → ∀ ref ∈ liveFields (evacuate input).1
      (canonical (evacuate input).1.copying owner), Safe input (evacuate input).1.copying ref) := by
  constructor
  · intro ref mem
    rw [evacuate_roots] at mem
    obtain ⟨owner, root, rfl⟩ := List.mem_map.mp mem
    exact canonical_safe input owner (root_retained input owner root)
  · intro owner retained ref mem
    rw [evacuate_children input owner retained] at mem
    obtain ⟨child, edge, rfl⟩ := List.mem_map.mp mem
    exact canonical_safe input child (retained_child input owner child retained edge)

theorem evacuate_identity (input : CollectionInput n) (a b : Fin n)
    (same : canonical (evacuate input).1.copying a = canonical (evacuate input).1.copying b) : a = b :=
  canonical_injective input.copyInput _ (evacuate_invariant input).tracking.copying a b same

theorem evacuate_scan_bound (input : CollectionInput n) : (evacuate input).2.length ≤ n := by
  rw [evacuate_trace, List.length_map]
  exact collect_scan_bound input.selectedGraph input.logicalRoots

theorem evacuate_trace_nodup (input : CollectionInput n) : (evacuate input).2.Nodup := by
  rw [evacuate_trace]
  exact List.Pairwise.map _ (fun a b ne same => ne (evacuate_identity input a b same))
    (collect_trace_nodup input.selectedGraph input.logicalRoots)

theorem evacuate_source_unchanged (input : CollectionInput n) (object : Fin n)
    (selected : input.selected object = true) (movable : input.copyInput.movable object = true) :
    (evacuate input).1.originalFields object = rawFields input.copyInput object := by
  have notRemembered : object ∉ input.remembered := by
    intro member
    have outside := remembered_unselected input object member
    simp [selected] at outside
  simpa [originalPlan, notRemembered, movable] using (evacuate_invariant input).fields.originals object

/-- Copies are old. Existing originals retain their generation. This is a
    logical retained-graph view, not a rewrite of discarded source headers. -/
def finalIsYoung (input : CollectionInput n) (object : Fin n) : Bool :=
  if (evacuate input).1.copying.forwarding object = none then input.heap.data.isYoung object else false

theorem retained_old (input : CollectionInput n) (object : Fin n) (retained : Retained input object) :
    finalIsYoung input object = false := by
  cases young : input.heap.data.isYoung object with
  | false => simp [finalIsYoung, young]
  | true =>
      have inside : input.selected object = true := by
        cases mode : input.collection <;> simp [CollectionInput.selected, mode, young]
      have live : Live input object := retained.resolve_left (by simp [inside])
      have movable : input.copyInput.movable object = true := by
        cases kind : input.heap.data.objectKind object <;>
          simp_all [RawGenerationalHeap.isYoung, OriginalKind.isYoung, CollectionInput.copyInput, OriginalKind.movable]
      obtain ⟨index, forward⟩ := (evacuate_forwarded_iff input object).mpr ⟨live, movable⟩
      simp [finalIsYoung, forward]

/-- Empty remembered membership covers every edge of the retained graph after
    promotion. Physical set retirement and raw source payloads are outside it. -/
theorem minor_reset_coverage (input : CollectionInput n) (owner child : Fin n)
    (retained : Retained input owner) (edge : child ∈ input.heap.data.graph.strongChildren owner) :
    finalIsYoung input child = false :=
  retained_old input child (retained_child input owner child retained edge)

end CloverGC.Generational
