import CloverGC.CopyDiscovery

namespace CloverGC.Copying

/-- Canonical location once discovered. Before discovery, this total function
    falls back to the original reference; proofs distinguish that case. -/
def canonical (state : CopyState n) (object : Fin n) : Reference n :=
  match state.forwarding object with
  | none => .source object
  | some index => .copied index

def repairReferences (input : CopyInput n) :
    CopyState n → List (Reference n) → CopyState n × List (Reference n)
  | state, [] => (state, [])
  | state, reference :: rest =>
      let (next, repaired) := discoverReference input state reference
      let (final, repairedRest) := repairReferences input next rest
      (final, repaired :: repairedRest)

structure EvacuationState (n : Nat) where
  copying : CopyState n
  originalFields : Fin n → List (Reference n)
  roots : List (Reference n)

def rawFields (input : CopyInput n) (object : Fin n) : List (Reference n) :=
  (input.heap.strongChildren object).map Reference.source

def liveFields (state : EvacuationState n) : Reference n → List (Reference n)
  | .source object => state.originalFields object
  | .copied index => (state.copying.copies[index]?).map CopyRecord.strongChildren |>.getD []

/-- Write into the latest state after child discoveries, preserving appended
    allocations. Only strong fields change, not origin/placement/forwarding. -/
def writeFields (state : EvacuationState n) (reference : Reference n)
    (fields : List (Reference n)) : EvacuationState n :=
  match reference with
  | .source object => { state with originalFields := fun other =>
      if other = object then fields else state.originalFields other }
  | .copied index => { state with copying := { state.copying with
      copies := state.copying.copies.modify index (fun record =>
        { record with strongChildren := fields }) } }

def seedRoots (input : CopyInput n) (roots : List (Fin n)) : EvacuationState n :=
  let (copying, repairedRoots) := repairReferences input .empty (roots.map Reference.source)
  ⟨copying, rawFields input, repairedRoots⟩

def scanCopy (input : CopyInput n) (state : EvacuationState n) :
    Option (Reference n × EvacuationState n) :=
  match state.copying.pending with
  | [] => none
  | reference :: rest =>
      let popped := { state.copying with pending := rest }
      let (copying, fields) := repairReferences input popped (liveFields state reference)
      some (reference, writeFields { state with copying := copying } reference fields)

def drainCopies (input : CopyInput n) :
    Nat → EvacuationState n → EvacuationState n × List (Reference n)
  | 0, state => (state, [])
  | fuel + 1, state =>
      match scanCopy input state with
      | none => (state, [])
      | some (reference, next) =>
          let (final, trace) := drainCopies input fuel next
          (final, reference :: trace)

def evacuate (input : CopyInput n) (roots : List (Fin n)) :
    EvacuationState n × List (Reference n) :=
  drainCopies input n (seedRoots input roots)

end CloverGC.Copying
