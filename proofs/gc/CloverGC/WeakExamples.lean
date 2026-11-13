import CloverGC.WeakRepairProofs

namespace CloverGC.Copying.Weak.Examples

/-- 0→1→2→0 is strongly live. 2 and 3 are NonMoving originals, but 3 is dead.
    Weak-only movable 4 owns child 5: neither may be retained through a weak unit. -/
def strongInput : CopyInput 6 := {
  heap := ⟨fun object => match object.val with
    | 0 => [1]
    | 1 => [2]
    | 2 => [0]
    | 4 => [5]
    | _ => []⟩
  movable := fun object => object.val != 2 && object.val != 3
  destination := fun object => if object.val = 1 then .nonMoving else .ordinary
}

/-- Adjacent owner units include a deferred guard and an already-resolved
    pointer payload. External entries exercise dead/live pointer payloads,
    inactive guards, null references, immediates and active immediate zero. -/
def weakInput : Input 6 14 := {
  templates := fun id => match id.val with
    | 0 => .entry ⟨some 0, some 2, .immediate 100⟩
    | 1 => .entry ⟨some 1, some 3, .immediate 200⟩
    | 2 => .entry ⟨some 1, some 1, .reference (some 1)⟩
    | 3 => .field (.reference (some 2))
    | 4 => .entry ⟨some 4, some 0, .reference (some 5)⟩
    | 5 => .entry ⟨some 0, some 0, .immediate 600⟩
    | 6 => .entry ⟨some 0, some 0, .reference (some 2)⟩
    | 7 => .entry ⟨none, some 0, .reference (some 4)⟩
    | 8 => .entry ⟨some 0, some 0, .reference (some 4)⟩
    | 9 => .entry ⟨some 0, some 0, .reference none⟩
    | 10 => .field (.immediate (-7))
    | 11 => .entry ⟨some 0, some 0, .immediate 0⟩
    | 12 => .field (.reference none)
    | _ => .entry ⟨some 0, some 0, .reference (some 1)⟩
  ownerUnits := fun object => match object.val with
    | 0 => [1, 2]
    | 1 => [3]
    | 4 => [5]
    | _ => []
  external := [0, 4, 6, 7, 8, 9, 10, 11, 12, 13]
}

theorem weakInput_valid : WellFormed weakInput := by
  constructor <;> decide

-- Duplicate registration is forbidden, rather than silently deduplicated.
example : ¬ WellFormed { weakInput with external := [0, 0] } := by
  intro wf
  have invalid : ¬ ([0, 0] : List (Fin 14)).Nodup := by decide
  exact invalid wf.external_nodup

example : ¬ WellFormed { weakInput with external := [1] } := by
  intro wf
  exact wf.external_disjoint 0 1 (by decide) (by decide)

example : ¬ WellFormed { weakInput with ownerUnits := fun _ => [1] } := by
  intro wf
  exact wf.owners_disjoint 0 1 (by decide) 1 (by decide) (by decide)

def seeded := start strongInput weakInput [0]
def traced := drainWeak strongInput weakInput 6 seeded
def result := collectWeak strongInput weakInput [0]

-- Early partial writes preserve unresolved positions, not all payload values.
example : seeded.units 0 = .entry ⟨some (.copied 0), some (.source 2), .immediate 100⟩ := by decide
example : seeded.units 6 = .entry ⟨some (.copied 0), some (.copied 0), .reference (some (.source 2))⟩ := by decide
example : seeded.units 8 = .entry ⟨some (.copied 0), some (.copied 0), .reference (some (.source 4))⟩ := by decide
example : seeded.units 13 = .entry ⟨some (.copied 0), some (.copied 0), .reference (some (.source 1))⟩ := by decide
example : seeded.units 7 = .entry ⟨none, none, .immediate 0⟩ := by decide
example : seeded.pending = [0, 4, 6, 8, 13] := by decide
example : seeded.registered = [0, 4, 6, 7, 8, 9, 10, 11, 12, 13] := by decide

-- Scanning 0 discovers 1 before visiting its adjacent units. The payload in
-- unit 2 relocates to the same NonMoving destination as both guards.
example : traced.registered = [0, 4, 6, 7, 8, 9, 10, 11, 12, 13, 1, 2, 3] := by decide
example : traced.pending = [0, 4, 6, 8, 13, 1] := by decide
example : traced.units 1 = .entry ⟨some (.copied 1), some (.source 3), .immediate 200⟩ := by decide
example : traced.units 2 = .entry ⟨some (.copied 1), some (.copied 1), .reference (some (.copied 1))⟩ := by decide
example : traced.units 3 = .field (.reference (some (.source 2))) := by decide

example : result.lateVisits = [0, 4, 6, 8, 13, 1] := by decide
example : result.pending = [] := by decide
example : result.units 0 = .entry ⟨some (.copied 0), some (.source 2), .immediate 100⟩ := by decide
example : result.units 1 = .entry ⟨none, none, .immediate 0⟩ := by decide
example : result.units 4 = .entry ⟨none, none, .immediate 0⟩ := by decide
-- A late-discovered live pointer payload survives; a dead payload resets even
-- an entry whose two guards are live. A null payload remains a null reference.
example : result.units 6 = .entry ⟨some (.copied 0), some (.copied 0), .reference (some (.source 2))⟩ := by decide
example : result.units 8 = .entry ⟨none, none, .immediate 0⟩ := by decide
example : result.units 9 = .entry ⟨some (.copied 0), some (.copied 0), .reference none⟩ := by decide
example : result.units 10 = .field (.immediate (-7)) := by decide
example : result.units 11 = .entry ⟨some (.copied 0), some (.copied 0), .immediate 0⟩ := by decide
example : result.units 12 = .field (.reference none) := by decide
-- A movable payload discovered after the external early visit relocates late.
example : result.units 13 = .entry ⟨some (.copied 0), some (.copied 0), .reference (some (.copied 1))⟩ := by decide

-- Weak-only targets and their descendants are never copied, marked or scanned.
example : result.strong.copying.copies.map CopyRecord.origin = [0, 1] := by decide
example : result.strong.copying.forwarding 4 = none ∧ result.strong.copying.forwarding 5 = none := by decide
example : Reference.source 3 ∉ result.strong.copying.marked := by decide

-- An unreachable owner's entry retains its initial source locations. It is
-- outside the surviving-unit safety claim and never roots its owner.
example : 5 ∉ result.registered := by decide
example : result.units 5 = .entry ⟨some (.source 0), some (.source 0), .immediate 600⟩ := by decide

def syntheticResolver : Reference 6 → Resolution 6
  | .source object => match object.val with
    | 0 => .resolved (.copied 0)
    | 1 => .resolved (.copied 1)
    | 3 => .dead
    | _ => .pending
  | .copied index => .resolved (.copied index)

-- Dead dominates Pending, whether the dead position is a guard or payload.
example : repairUnit syntheticResolver
    (.entry ⟨some (.source 4), some (.source 3), .reference (some (.source 0))⟩) =
    (.entry ⟨none, none, .immediate 0⟩, .complete) := by decide
example : repairUnit syntheticResolver
    (.entry ⟨some (.source 4), some (.source 4), .reference (some (.source 3))⟩) =
    (.entry ⟨none, none, .immediate 0⟩, .complete) := by decide
-- Resolved positions after a Pending one still receive their partial writes.
example : repairUnit syntheticResolver
    (.entry ⟨some (.source 4), some (.source 1), .reference (some (.source 0))⟩) =
    (.entry ⟨some (.source 4), some (.copied 1), .reference (some (.copied 0))⟩, .pending) := by decide

-- Each inactive shape resets independently of the resolver, including live or
-- stale pointer payloads and nonzero immediate payloads.
example (resolver : Reference 6 → Resolution 6) :
    repairUnit resolver (.entry ⟨none, some (.source 0), .reference (some (.source 4))⟩) =
      (.entry ⟨none, none, .immediate 0⟩, .complete) := rfl
example (resolver : Reference 6 → Resolution 6) :
    repairUnit resolver (.entry ⟨some (.source 0), none, .immediate 7⟩) =
      (.entry ⟨none, none, .immediate 0⟩, .complete) := rfl
example (resolver : Reference 6 → Resolution 6) :
    repairUnit resolver (.entry ⟨none, none, .reference none⟩) =
      (.entry ⟨none, none, .immediate 0⟩, .complete) := rfl

example : repairUnit syntheticResolver (.field (.reference (some (.source 3)))) =
    (.field (.reference none), .complete) := by decide
example : repairUnit syntheticResolver (.field (.reference none)) =
    (.field (.reference none), .complete) := by decide
example : repairUnit syntheticResolver (.field (.immediate 7)) =
    (.field (.immediate 7), .complete) := by decide

-- Mobility and liveness are independent, including for pointer payloads.
example : resolve strongInput seeded.strong.copying .early (.source 2) = .pending := by decide
example : resolve strongInput result.strong.copying .late (.source 2) = .resolved (.source 2) := by decide
example : resolve strongInput result.strong.copying .late (.source 3) = .dead := by decide

-- External entries do not keep the 0→1→2 cycle alive without strong roots.
example : (collectWeak strongInput weakInput []).strong.copying.marked = [] := by decide
example : (collectWeak strongInput weakInput []).registered = weakInput.external := by decide
example : (collectWeak strongInput weakInput []).units 0 = .entry ⟨none, none, .immediate 0⟩ := by decide

end CloverGC.Copying.Weak.Examples
