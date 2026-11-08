import CloverGC.GenerationalWeak
import CloverGC.GenerationalWeakHeap

namespace CloverGC.Generational.Weak

open Copying

structure Input (n k : Nat) where
  heap : WeakHeap n k
  collection : CollectionKind
  roots : List (Fin n)

def Input.strongInput (input : Input n k) : CollectionInput n :=
  ⟨input.heap.data.strong, input.collection, input.roots⟩

def Input.remembered (input : Input n k) : List (Fin k) :=
  match input.collection with
  | .full => []
  | .young => input.heap.data.rememberedWeak

def earlyVisit (input : Input n k) (state : Copying.Weak.State n k) (id : Fin k) :
    Copying.Weak.State n k :=
  let (unit, status) := Copying.Weak.repairUnit
    (resolve input.strongInput state.strong.copying .early) (state.units id)
  { state with
    units := fun other => if other = id then unit else state.units other
    pending := if status = .pending then state.pending ++ [id] else state.pending
    registered := state.registered ++ [id] }

def earlyVisits (input : Input n k) :
    Copying.Weak.State n k → List (Fin k) → Copying.Weak.State n k
  | state, [] => state
  | state, id :: rest => earlyVisits input (earlyVisit input state id) rest

def lateVisit (input : Input n k) (state : Copying.Weak.State n k) (id : Fin k) :
    Copying.Weak.State n k :=
  let (unit, _) := Copying.Weak.repairUnit
    (resolve input.strongInput state.strong.copying .late) (state.units id)
  { state with
    units := fun other => if other = id then unit else state.units other
    lateVisits := state.lateVisits ++ [id] }

def lateVisits (input : Input n k) :
    Copying.Weak.State n k → List (Fin k) → Copying.Weak.State n k
  | state, [] => state
  | state, id :: rest => lateVisits input (lateVisit input state id) rest

/-- Register explicit units before roots. Remembered weak units are supplied
    only in young mode; full mode reaches old owner storage through scanning. -/
def start (input : Input n k) : Copying.Weak.State n k :=
  let initial := earlyVisits input
    ⟨⟨.empty, rawFields input.strongInput.copyInput, []⟩,
      fun id => (input.heap.data.units.templates id).initial, [], [], []⟩
    (input.heap.data.units.external ++ input.remembered)
  { initial with strong := Generational.start input.strongInput }

def drainWeak (input : Input n k) : Nat → Copying.Weak.State n k →
    Copying.Weak.State n k × List (Reference n)
  | 0, state => (state, [])
  | fuel + 1, state =>
      match Generational.scan input.strongInput state.strong with
      | none => (state, [])
      | some (reference, next) =>
          let visited := earlyVisits input { state with strong := next }
            (Copying.Weak.ownerUnitsFor input.heap.data.units next reference)
          let (final, trace) := drainWeak input fuel visited
          (final, reference :: trace)

def finishDeferred (input : Input n k) (state : Copying.Weak.State n k) : Copying.Weak.State n k :=
  { lateVisits input state state.pending with pending := [] }

/-- One legal C++ schedule: external/remembered weak storage, strong roots and
    remembered fields, per-scan owner units, then deferred repair at closure. -/
def collectWeak (input : Input n k) : Copying.Weak.State n k × List (Reference n) :=
  let (state, trace) := drainWeak input n (start input)
  (finishDeferred input state, trace)

end CloverGC.Generational.Weak
