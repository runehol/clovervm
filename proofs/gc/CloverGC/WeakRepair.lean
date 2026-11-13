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

/-- A tagged cell. Null reference cells and immediate zero remain distinct. -/
inductive Cell (α : Type) where
  | immediate : Int64 → Cell α
  | reference : Option α → Cell α
  deriving DecidableEq, Repr

def Cell.map (f : α → β) : Cell α → Cell β
  | .immediate value => .immediate value
  | .reference target => .reference (target.map f)

def Cell.target : Cell α → Option α
  | .immediate _ => none
  | .reference target => target

abbrev WeakGuard (α : Type) := Option α
abbrev WeakField (α : Type) := Cell α

/-- Fixed IC-like storage: two mandatory guards and a tagged payload. All
    three references are weak; the payload never contributes a strong edge. -/
structure Entry (α : Type) where
  firstGuard : WeakGuard α
  secondGuard : WeakGuard α
  payload : WeakField α
  deriving DecidableEq, Repr

abbrev WeakEntry (n : Nat) := Entry (Reference n)

/-- A standalone optional/tagged weak field or a coordinated IC-like record.
    The aliases describe slot roles, not a weak bit in the stored value. -/
inductive RepairUnit (α : Type) where
  | field : WeakField α → RepairUnit α
  | entry : Entry α → RepairUnit α
  deriving DecidableEq, Repr

abbrev Unit (n : Nat) := RepairUnit (Reference n)
/-- Input templates contain original IDs, never arbitrary copied indices. -/
abbrev Template (n : Nat) := RepairUnit (Fin n)

def RepairUnit.map (f : α → β) : RepairUnit α → RepairUnit β
  | .field cell => .field (cell.map f)
  | .entry record => .entry ⟨record.firstGuard.map f, record.secondGuard.map f, record.payload.map f⟩

/-- Reference-bearing positions in callback order. Immediates contribute none. -/
def RepairUnit.targets : RepairUnit α → List (Option α)
  | .field cell => [cell.target]
  | .entry record => [record.firstGuard, record.secondGuard, record.payload.target]

/-- Null mandatory guards invalidate an entry, but not a standalone field. -/
def RepairUnit.active : RepairUnit α → Bool
  | .field _ => true
  | .entry record => record.firstGuard.isSome && record.secondGuard.isSome

def Template.initial (template : Template n) : Unit n :=
  template.map Reference.source

def clear : Unit n → Unit n
  | .field (.immediate value) => .field (.immediate value)
  | .field (.reference _) => .field (.reference none)
  | .entry _ => .entry ⟨none, none, .immediate 0⟩

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

structure CellResult (n : Nat) where
  cell : Cell (Reference n)
  pending : Bool
  dead : Bool
  deriving DecidableEq, Repr

def repairCell (resolver : Reference n → Resolution n) : WeakField (Reference n) → CellResult n
  | .immediate value => ⟨.immediate value, false, false⟩
  | .reference target =>
      let result := repairField resolver target
      ⟨.reference result.target, result.pending, result.dead⟩

/-- Combine all three inspections only after their results are available. -/
def finishEntry (first second : FieldResult n) (payload : CellResult n) : Unit n × Status :=
  if first.dead || second.dead || payload.dead then
    (.entry ⟨none, none, .immediate 0⟩, .complete)
  else (.entry ⟨first.target, second.target, payload.cell⟩,
    if first.pending || second.pending || payload.pending then .pending else .complete)

/-- Inactive entries reset without consulting the resolver. Active entries
    inspect guard 0, guard 1, then the tagged payload, without short-circuiting.
    Resolved positions are updated even when another position is Pending;
    Dead dominates Pending and resets the entire entry to null/null/immediate 0.
    Standalone fields have no mandatory guards and retain immediate values. -/
def repairUnit (resolver : Reference n → Resolution n) : Unit n → Unit n × Status
  | .field cell =>
      let result := repairCell resolver cell
      (.field result.cell, if result.pending then .pending else .complete)
  | .entry record =>
      if record.firstGuard.isNone || record.secondGuard.isNone then
        (clear (.entry record), .complete)
      else
        let first := repairField resolver record.firstGuard
        let second := repairField resolver record.secondGuard
        let payload := repairCell resolver record.payload
        finishEntry first second payload

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
