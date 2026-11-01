import CloverGC.Evacuation

namespace CloverGC.Copying.Weak

inductive Phase where
  | early
  | late
  deriving DecidableEq, Repr

inductive Resolution (n : Nat) where
  | resolved : Reference n → Resolution n
  | pending
  | dead
  deriving DecidableEq, Repr

inductive Status where
  | complete
  | pending
  deriving DecidableEq, Repr

def unresolved (phase : Phase) : Resolution n :=
  match phase with
  | .early => .pending
  | .late => .dead

/-- Full-selection resolver: no marking, allocation, or strong discovery.
    Copied inputs must be valid; all allocated copies are already marked. -/
def resolve (input : CopyInput n) (copying : CopyState n) (phase : Phase) :
    Reference n → Resolution n
  | .copied index => .resolved (.copied index)
  | .source object =>
      if input.movable object then
        match copying.forwarding object with
        | some index => .resolved (.copied index)
        | none => unresolved phase
      else if Reference.source object ∈ copying.marked then .resolved (.source object)
      else unresolved phase

structure Unit (n : Nat) where
  targets : List (Option (Reference n))
  payload : Option Nat
  deriving DecidableEq, Repr

/-- Input templates contain original object IDs, never arbitrary copied indices. -/
structure Template (n : Nat) where
  targets : List (Option (Fin n))
  payload : Option Nat

def Template.initial (template : Template n) : Unit n :=
  ⟨template.targets.map (Option.map Reference.source), template.payload⟩

def clear (unit : Unit n) : Unit n :=
  ⟨unit.targets.map (fun _ => none), none⟩

structure FieldResult (n : Nat) where
  target : Option (Reference n)
  pending : Bool
  dead : Bool
  deriving DecidableEq, Repr

def repairField (resolver : Reference n → Resolution n) : Option (Reference n) → FieldResult n
  | none => ⟨none, false, false⟩
  | some reference =>
      match resolver reference with
      | .resolved target => ⟨some target, false, false⟩
      | .pending => ⟨some reference, true, false⟩
      | .dead => ⟨none, false, true⟩

/-- All fields are repaired before combining statuses. Dead dominates Pending
    and clears the entire unit, including payload, not just individual slots. -/
def repairUnit (resolver : Reference n → Resolution n) (unit : Unit n) : Unit n × Status :=
  let results := unit.targets.map (repairField resolver)
  if results.any FieldResult.dead then (clear unit, .complete)
  else (⟨results.map FieldResult.target, unit.payload⟩,
    if results.any FieldResult.pending then .pending else .complete)

structure Input (n k : Nat) where
  templates : Fin k → Template n
  ownerUnits : Fin n → List (Fin k)
  external : List (Fin k)

/-- Stable IDs stand for disjoint repair storage. Each unit is supplied once. -/
structure WellFormed (input : Input n k) : Prop where
  external_nodup : input.external.Nodup
  owner_nodup : ∀ object, (input.ownerUnits object).Nodup
  owners_disjoint : ∀ left right, left ≠ right →
    ∀ id ∈ input.ownerUnits left, id ∉ input.ownerUnits right
  external_disjoint : ∀ object, ∀ id ∈ input.external, id ∉ input.ownerUnits object

structure State (n k : Nat) where
  strong : EvacuationState n
  units : Fin k → Unit n
  pending : List (Fin k)
  -- Audit histories, never consulted by scheduling or repair.
  registered : List (Fin k)
  lateVisits : List (Fin k)

def earlyVisit (input : CopyInput n) (state : State n k) (id : Fin k) : State n k :=
  let (unit, status) := repairUnit (resolve input state.strong.copying .early) (state.units id)
  { state with
    units := fun other => if other = id then unit else state.units other
    pending := if status = .pending then state.pending ++ [id] else state.pending
    registered := state.registered ++ [id] }

def earlyVisits (input : CopyInput n) : State n k → List (Fin k) → State n k
  | state, [] => state
  | state, id :: rest => earlyVisits input (earlyVisit input state id) rest

def lateVisit (input : CopyInput n) (state : State n k) (id : Fin k) : State n k :=
  let (unit, _) := repairUnit (resolve input state.strong.copying .late) (state.units id)
  { state with
    units := fun other => if other = id then unit else state.units other
    lateVisits := state.lateVisits ++ [id] }

def lateVisits (input : CopyInput n) : State n k → List (Fin k) → State n k
  | state, [] => state
  | state, id :: rest => lateVisits input (lateVisit input state id) rest

def ownerUnitsFor (input : Input n k) (strong : EvacuationState n) : Reference n → List (Fin k)
  | .source object => input.ownerUnits object
  | .copied index =>
      ((strong.copying.copies[index]?).map (fun record => input.ownerUnits record.origin)).getD []

def drainWeak (input : CopyInput n) (weak : Input n k) : Nat → State n k → State n k
  | 0, state => state
  | fuel + 1, state =>
      match scanCopy input state.strong with
      | none => state
      | some (reference, next) =>
          let visited := earlyVisits input { state with strong := next }
            (ownerUnitsFor weak next reference)
          drainWeak input weak fuel visited

def start (input : CopyInput n) (weak : Input n k) (roots : List (Fin n)) : State n k :=
  earlyVisits input ⟨seedRoots input roots, fun id => (weak.templates id).initial, [], [], []⟩ weak.external

def finishDeferred (input : CopyInput n) (state : State n k) : State n k :=
  { lateVisits input state state.pending with pending := [] }

/-- One legal C++ schedule: roots, external weak units, per-scan owner units,
    then deferred repair after strong closure. No new roots after this point. -/
def collectWeak (input : CopyInput n) (weak : Input n k) (roots : List (Fin n)) : State n k :=
  finishDeferred input (drainWeak input weak n (start input weak roots))

end CloverGC.Copying.Weak
