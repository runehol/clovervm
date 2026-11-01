import CloverGC.Mark

namespace CloverGC.Copying

/-- Logical source identities and physical destination indices are distinct.
    A copied index is valid only if the destination record has been allocated. -/
inductive Reference (n : Nat) where
  | source : Fin n → Reference n
  | copied : Nat → Reference n
  deriving Repr, DecidableEq

inductive DestinationKind where
  | ordinary
  | nonMoving
  deriving Repr, DecidableEq

structure CopyRecord (n : Nat) where
  origin : Fin n
  destination : DestinationKind
  strongChildren : List (Reference n)
  deriving Repr, DecidableEq

/-- All original objects participate in this slice's collection. Movability
    and destination placement are independent, fixed input policies. -/
structure CopyInput (n : Nat) where
  heap : Heap n
  movable : Fin n → Bool
  destination : Fin n → DestinationKind

def CopyInput.makeCopy (input : CopyInput n) (object : Fin n) : CopyRecord n :=
  ⟨object, input.destination object,
    (input.heap.strongChildren object).map Reference.source⟩

structure CopyState (n : Nat) where
  forwarding : Fin n → Option Nat
  copies : List (CopyRecord n)
  marked : List (Reference n)
  pending : List (Reference n)

def CopyState.empty : CopyState n := ⟨fun _ => none, [], [], []⟩

/-- Mark the canonical object before returning a reference to it. Ordinary
    and NonMoving destinations share exactly the same work queue. -/
def enqueueOnce (reference : Reference n) (state : CopyState n) : CopyState n :=
  if reference ∈ state.marked then state
  else { state with
    marked := reference :: state.marked
    pending := state.pending ++ [reference] }

/-- Allocate at a fresh sequential destination index and publish forwarding.
    This intermediate state is not exposed by discoverReference: discovery
    marks/enqueues the copy before returning it to the caller. -/
def installCopy (input : CopyInput n) (state : CopyState n) (object : Fin n) :
    CopyState n :=
  { state with
    forwarding := fun other =>
      if other = object then some state.copies.length else state.forwarding other
    copies := state.copies ++ [input.makeCopy object] }

/-- The copied-reference arm requires an allocated canonical reference. Invalid
    indices can be expressed, but are excluded by the public proof preconditions. -/
def discoverReference (input : CopyInput n) (state : CopyState n) :
    Reference n → CopyState n × Reference n
  | .copied index => (state, .copied index)
  | .source object =>
      if input.movable object then
        match state.forwarding object with
        | some index =>
            (enqueueOnce (.copied index) state, .copied index)
        | none =>
            let index := state.copies.length
            (enqueueOnce (.copied index) (installCopy input state object), .copied index)
      else (enqueueOnce (.source object) state, .source object)

/-- Original references are always in range; destination references must name
    an allocated record. This is a proof precondition, not a runtime check. -/
def ValidReference (state : CopyState n) : Reference n → Prop
  | .source _ => True
  | .copied index => ∃ record, state.copies[index]? = some record

/-- This slice's canonical objects: unmoved originals or allocated copies.
    Destination placement does not affect canonicality. -/
def Canonical (input : CopyInput n) (state : CopyState n) : Reference n → Prop
  | .source object => input.movable object = false
  | .copied index => ∃ record, state.copies[index]? = some record

/-- A canonical reference denotes this logical original object. -/
def Represents (input : CopyInput n) (state : CopyState n) (object : Fin n) :
    Reference n → Prop
  | .source other => other = object ∧ input.movable object = false
  | .copied index => state.forwarding object = some index

/-- The copy ledger is a partial one-to-one correspondence. Records retain
    the initial source edges: scanning/rewriting copied children comes later. -/
structure Ledger (input : CopyInput n) (state : CopyState n) : Prop where
  forwarding_valid : ∀ object index, state.forwarding object = some index →
    ∃ record, state.copies[index]? = some record ∧ record.origin = object
  copies_forwarded : ∀ index record, state.copies[index]? = some record →
    state.forwarding record.origin = some index
  copies_faithful : ∀ (index : Nat) (record : CopyRecord n), state.copies[index]? = some record →
    input.movable record.origin = true ∧ record = input.makeCopy record.origin

structure CopyInvariant (input : CopyInput n) (state : CopyState n) : Prop where
  ledger : Ledger input state
  marked_canonical : ∀ reference ∈ state.marked, Canonical input state reference
  copies_marked : ∀ index record, state.copies[index]? = some record →
    Reference.copied index ∈ state.marked
  marked_nodup : state.marked.Nodup
  pending_nodup : state.pending.Nodup
  pending_marked : ∀ reference ∈ state.pending, reference ∈ state.marked

end CloverGC.Copying
