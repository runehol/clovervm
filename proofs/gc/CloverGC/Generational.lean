import CloverGC.GenerationalHeap
import CloverGC.Evacuation

namespace CloverGC.Generational

open Copying

inductive CollectionKind where
  | full | young
  deriving Repr, DecidableEq, BEq

structure CollectionInput (n : Nat) where
  heap : GenerationalHeap n
  collection : CollectionKind
  roots : List (Fin n)

def CollectionInput.copyInput (input : CollectionInput n) : CopyInput n :=
  ⟨input.heap.data.graph, fun o => (input.heap.data.objectKind o).movable,
    fun o => (input.heap.data.objectKind o).destination⟩
def CollectionInput.selected (input : CollectionInput n) (o : Fin n) : Bool :=
  match input.collection with
  | .full => true
  | .young => input.heap.data.isYoung o
def CollectionInput.remembered (input : CollectionInput n) : List (Fin n) :=
  match input.collection with
  | .full => []
  | .young => input.heap.data.rememberedOwners

/-- The logical oracle visits only selected vertices, seeded by actual root
    slots and the fields of remembered owners. Old owners are not scanned. -/
def CollectionInput.selectedGraph (input : CollectionInput n) : Heap n :=
  ⟨fun o => (input.heap.data.graph.strongChildren o).filter input.selected⟩
def CollectionInput.logicalRoots (input : CollectionInput n) : List (Fin n) :=
  (input.roots ++ input.remembered.flatMap input.heap.data.graph.strongChildren).filter input.selected

/-- Reuse the proved forwarding/allocation primitive only for selected sources.
    Unselected references are returned unchanged without marking or enqueueing. -/
def discover (input : CollectionInput n) (state : CopyState n) :
    Reference n → CopyState n × Reference n
  | .source o => if input.selected o then discoverReference input.copyInput state (.source o)
      else (state, .source o)
  | .copied j => (state, .copied j)

def repair (input : CollectionInput n) :
    CopyState n → List (Reference n) → CopyState n × List (Reference n)
  | state, [] => (state, [])
  | state, ref :: rest =>
      let (next, ref') := discover input state ref
      let (final, rest') := repair input next rest
      (final, ref' :: rest')

def seedRoots (input : CollectionInput n) : EvacuationState n :=
  let (copying, roots) := repair input .empty (input.roots.map Reference.source)
  ⟨copying, rawFields input.copyInput, roots⟩

def remember (input : CollectionInput n) : EvacuationState n → List (Fin n) → EvacuationState n
  | state, [] => state
  | state, owner :: rest =>
      let (copying, fields) := repair input state.copying (state.originalFields owner)
      remember input (writeFields { state with copying := copying } (.source owner) fields) rest

def start (input : CollectionInput n) : EvacuationState n :=
  remember input (seedRoots input) input.remembered

def scan (input : CollectionInput n) (state : EvacuationState n) :
    Option (Reference n × EvacuationState n) :=
  match state.copying.pending with
  | [] => none
  | ref :: rest =>
      let popped := { state.copying with pending := rest }
      let (copying, fields) := repair input popped (liveFields state ref)
      some (ref, writeFields { state with copying := copying } ref fields)

def drain (input : CollectionInput n) : Nat → EvacuationState n → EvacuationState n × List (Reference n)
  | 0, state => (state, [])
  | fuel + 1, state =>
      match scan input state with
      | none => (state, [])
      | some (ref, next) =>
          let (final, trace) := drain input fuel next
          (final, ref :: trace)

def evacuate (input : CollectionInput n) : EvacuationState n × List (Reference n) :=
  drain input n (start input)

end CloverGC.Generational
