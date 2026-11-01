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

def weakInput : Input 6 6 := {
  templates := fun id => match id.val with
    | 0 => ⟨[some 0, some 2, none], some 100⟩
    | 1 => ⟨[some 1, some 3], some 200⟩
    | 2 => ⟨[some 1], some 300⟩
    | 3 => ⟨[some 2], some 400⟩
    | 4 => ⟨[some 4], some 500⟩
    | _ => ⟨[some 0], some 600⟩
  ownerUnits := fun object => match object.val with
    | 0 => [1, 2]
    | 1 => [3]
    | 4 => [5]
    | _ => []
  external := [0, 4]
}

theorem weakInput_valid : WellFormed weakInput := by
  constructor <;> decide

-- Duplicate registration is a forbidden input, not silently deduplicated by
-- the executable model or assumed away without an explicit theorem premise.
example : ¬ WellFormed { weakInput with external := [0, 0] } := by
  intro wf
  have invalid : ¬ ([0, 0] : List (Fin 6)).Nodup := by decide
  exact invalid wf.external_nodup

def seeded := start strongInput weakInput [0]
def traced := drainWeak strongInput weakInput 6 seeded
def result := collectWeak strongInput weakInput [0]

-- Early partial repair preserves unresolved fields and the payload exactly.
example : (seeded.units 0).targets = [some (.copied 0), some (.source 2), none] := by decide
example : (seeded.units 0).payload = some 100 := by decide
example : seeded.pending = [0, 4] := by decide
example : seeded.registered = [0, 4] := by decide

-- Scanning 0 discovers movable 1 before enumerating the adjacent units 1 and 2.
-- One defers and one completes: never defer/rescan their entire common owner.
example : traced.registered = [0, 4, 1, 2, 3] := by decide
example : traced.pending = [0, 4, 1] := by decide
example : (traced.units 1).targets = [some (.copied 1), some (.source 3)] := by decide
example : (traced.units 2).targets = [some (.copied 1)] := by decide
example : (traced.units 3).targets = [some (.source 2)] := by decide

example : result.lateVisits = [0, 4, 1] := by decide
example : result.pending = [] := by decide
example : (result.units 0).targets = [some (.copied 0), some (.source 2), none] := by decide
example : (result.units 0).payload = some 100 := by decide
example : (result.units 1).targets = [none, none] := by decide
example : (result.units 1).payload = none := by decide
example : (result.units 2).payload = some 300 := by decide
example : (result.units 3).payload = some 400 := by decide
example : (result.units 4).targets = [none] := by decide
example : (result.units 4).payload = none := by decide

-- Weak-only targets and their descendants do not get copied or marked.
example : result.strong.copying.copies.map CopyRecord.origin = [0, 1] := by decide
example : result.strong.copying.forwarding 4 = none ∧ result.strong.copying.forwarding 5 = none := by decide
example : Reference.source 3 ∉ result.strong.copying.marked := by decide

-- The unreachable owner's unit is not enumerated. Its stale pointer is not a
-- surviving weak reference; the owner is not resurrected by the unit registry.
example : 5 ∉ result.registered := by decide
example : (result.units 5).targets = [some (.source 0)] := by decide

def syntheticResolver : Reference 6 → Resolution 6
  | .source object => if object.val = 3 then .dead else .pending
  | .copied index => .resolved (.copied index)

-- Pending first does not hide a later Dead field: clear the whole payload.
example : repairUnit syntheticResolver ⟨[some (.source 4), some (.source 3)], some 999⟩ =
    (⟨[none, none], none⟩, .complete) := by decide

example : repairUnit syntheticResolver ⟨[none], some 7⟩ =
    (⟨[none], some 7⟩, .complete) := by decide

-- Logical target 2 is strongly live despite initially being Pending; NonMoving
-- target 3 is dead despite being immovable. Liveness and mobility are distinct.
example : resolve strongInput seeded.strong.copying .early (.source 2) = .pending := by decide
example : resolve strongInput result.strong.copying .late (.source 2) = .resolved (.source 2) := by decide
example : resolve strongInput result.strong.copying .late (.source 3) = .dead := by decide

-- External weak units do not keep the whole 0→1→2 cycle alive in the absence
-- of strong roots, even though their storage is supplied to the collector.
example : (collectWeak strongInput weakInput []).strong.copying.marked = [] := by decide
example : (collectWeak strongInput weakInput []).registered = [0, 4] := by decide
example : ((collectWeak strongInput weakInput []).units 0).payload = none := by decide

end CloverGC.Copying.Weak.Examples
