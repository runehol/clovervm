import Std

namespace CloverGC

/-- A fixed heap after decoding strong reference slots. `Fin n` excludes
    dangling references: every object ID is in the range [0, n). -/
structure Heap (n : Nat) where
  strongChildren : Fin n → List (Fin n)

/-- An independent graph specification, not defined in terms of marking. -/
inductive Reachable (heap : Heap n) (roots : List (Fin n)) : Fin n → Prop where
  | root {object} : object ∈ roots → Reachable heap roots object
  | edge {parent child} : Reachable heap roots parent →
      child ∈ heap.strongChildren parent → Reachable heap roots child

structure MarkState (n : Nat) where
  marked : List (Fin n)
  pending : List (Fin n)
  deriving Repr, DecidableEq

def MarkState.empty : MarkState n := ⟨[], []⟩

/-- Mark before enqueueing, just like StrongClosure::discover. Marked-list
    order is irrelevant; pending-list order is FIFO. -/
def discover (object : Fin n) (state : MarkState n) : MarkState n :=
  if object ∈ state.marked then state
  else ⟨object :: state.marked, state.pending ++ [object]⟩

def discoverMany : List (Fin n) → MarkState n → MarkState n
  | [], state => state
  | object :: rest, state => discoverMany rest (discover object state)

def seed (roots : List (Fin n)) : MarkState n :=
  discoverMany roots .empty

/-- Scan one queued object. None means strong closure has been reached. -/
def scanOne (heap : Heap n) (state : MarkState n) :
    Option (Fin n × MarkState n) :=
  match state.pending with
  | [] => none
  | object :: rest =>
      some (object, discoverMany (heap.strongChildren object)
        ⟨state.marked, rest⟩)

/-- Bounded executable driver. The returned trace records actual queue pops;
    it is audit data, not another mark set or an input to discovery. -/
def drain (heap : Heap n) : Nat → MarkState n → MarkState n × List (Fin n)
  | 0, state => (state, [])
  | fuel + 1, state =>
      match scanOne heap state with
      | none => (state, [])
      | some (object, next) =>
          let (final, trace) := drain heap fuel next
          (final, object :: trace)

/-- The proof shows n queue pops suffice for every heap with n objects. -/
def collect (heap : Heap n) (roots : List (Fin n)) :
    MarkState n × List (Fin n) :=
  drain heap n (seed roots)

end CloverGC
