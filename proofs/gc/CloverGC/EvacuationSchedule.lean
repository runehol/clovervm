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
  simpa only [discover, MarkedQueue.enqueue, apply_ite] using
    MarkedQueue.included object logical.marked logical.pending included

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

end CloverGC.Copying
