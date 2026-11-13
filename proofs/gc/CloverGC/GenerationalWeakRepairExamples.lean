import CloverGC.GenerationalWeakRepair

namespace CloverGC.Generational.Weak.RepairExamples

open Copying Copying.Weak

private def heap : GenerationalHeap 4 := {
  data := ⟨⟨fun _ => []⟩, fun o => if o = 0 then .oldMovable .ordinary
    else if o = 3 then .nonmovingOld else .youngMovable .ordinary, []⟩
  valid := by decide }
private def minor : CollectionInput 4 := ⟨heap, .young, []⟩
private def before : CopyState 4 := (discoverReference minor.copyInput .empty (.source 1)).1
private def after : CopyState 4 := (discoverReference minor.copyInput before (.source 2)).1
private theorem advanced : Progress before after := discovery_progress minor.copyInput before (.source 2)

private def mixed : Unit 4 :=
  .entry ⟨some (.source 0), some (.source 2), .reference (some (.source 1))⟩
private def settled : Unit 4 :=
  .entry ⟨some (.source 0), some (.source 3), .reference (some (.source 1))⟩

-- Pending guard 1 does not prevent payload relocation; the unmarked old guard
-- is already resolved. Later strong progress makes the remaining guard live.
example : repairUnit (resolve minor before .early) mixed =
    (.entry ⟨some (.source 0), some (.source 2), .reference (some (.copied 0))⟩, .pending) := by decide
example : repairUnit (resolve minor after .late) mixed =
    (.entry ⟨some (.source 0), some (.copied 1), .reference (some (.copied 0))⟩, .complete) := by decide
example : repairUnit (resolve minor after .late) (repairUnit (resolve minor before .early) mixed).1 =
    repairUnit (resolve minor after .late) mixed := early_unit_compatible minor before after advanced mixed

-- Without that strong discovery, the dead guard resets every entry position.
example : repairUnit (resolve minor before .late) mixed =
    (.entry ⟨none, none, .immediate 0⟩, .complete) := by decide
example : repairUnit (resolve minor before .late) (repairUnit (resolve minor before .early) mixed).1 =
    repairUnit (resolve minor before .late) mixed :=
  early_unit_compatible minor before before (Progress.refl before) mixed

-- Early Complete needs no queued revisit, including an already-copied payload.
example : (repairUnit (resolve minor before .early) settled).2 = .complete := by decide
example : (repairUnit (resolve minor before .early) settled).1 =
    (repairUnit (resolve minor after .late) settled).1 :=
  early_complete_correct minor before after advanced settled (by decide)

-- Immediates and null references remain distinct; neither can defer repair.
example : repairUnit (resolve minor after .late) (.field (.immediate 0)) =
    (.field (.immediate 0), .complete) := by decide
example : repairUnit (resolve minor after .late) (.field (.reference none)) =
    (.field (.reference none), .complete) := by decide
example : repairUnit (resolve minor before .early)
    (.entry ⟨none, some (.source 2), .reference (some (.source 1))⟩) =
    (.entry ⟨none, none, .immediate 0⟩, .complete) := by decide
example : (repairUnit (resolve minor before .early)
    (.entry ⟨some (.source 0), some (.source 3), .immediate 17⟩)).1 =
    .entry ⟨some (.source 0), some (.source 3), .immediate 17⟩ := by decide

-- Full mode still treats unmarked old targets as dead at the late phase.
example : repairUnit (resolve { minor with collection := .full } before .late) settled =
    (.entry ⟨none, none, .immediate 0⟩, .complete) := by decide
example (unit : Unit 4) : (repairUnit (resolve minor after .late) unit).2 = .complete :=
  repair_late_complete minor after unit

end CloverGC.Generational.Weak.RepairExamples
