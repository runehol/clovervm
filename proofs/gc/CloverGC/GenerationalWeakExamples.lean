import CloverGC.GenerationalWeakProofs

namespace CloverGC.Generational.Weak.Examples

open Copying Copying.Weak

private def heap : GenerationalHeap 3 := {
  data := {
    graph := ⟨fun _ => []⟩
    objectKind := fun o => if o = 0 then .oldMovable .ordinary
      else if o = 1 then .youngMovable .nonMoving else .nonmovingOld
    rememberedOwners := [] }
  valid := by decide }

private def minor : CollectionInput 3 := ⟨heap, .young, []⟩
private def full : CollectionInput 3 := ⟨heap, .full, []⟩
private def copied : CopyState 3 := (Generational.discover minor .empty (.source 1)).1

-- Both kinds of old object resolve without a mark, in either phase.
example : resolve minor .empty .early (.source 0) = .resolved (.source 0) := by decide
example : resolve minor .empty .late (.source 0) = .resolved (.source 0) := by decide
example : resolve minor .empty .early (.source 2) = .resolved (.source 2) := by decide
example : resolve minor .empty .late (.source 2) = .resolved (.source 2) := by decide

-- Selection still matters in full mode, including for existing nonmoving objects.
example : resolve full .empty .early (.source 0) = .pending := by decide
example : resolve full .empty .late (.source 0) = .dead := by decide
example : resolve full .empty .early (.source 2) = .pending := by decide
example : resolve full .empty .late (.source 2) = .dead := by decide
example : resolve full { CopyState.empty with marked := [.source 2] } .late (.source 2) =
    .resolved (.source 2) := by decide

-- Undiscovered young targets defer, then die; strong discovery makes their copy live.
example : resolve minor .empty .early (.source 1) = .pending := by decide
example : resolve minor .empty .late (.source 1) = .dead := by decide
example : resolve minor copied .early (.source 1) = .resolved (.copied 0) := by decide
example : resolve minor copied .late (.source 1) = .resolved (.copied 0) := by decide
example : resolve minor copied .late (.copied 0) = .resolved (.copied 0) := by decide

private def guardEntry : Unit 3 := .entry ⟨some (.source 0), some (.source 1), .immediate 7⟩
private def payloadEntry : Unit 3 :=
  .entry ⟨some (.source 0), some (.source 2), .reference (some (.source 1))⟩

example : repairUnit (resolve minor .empty .early) guardEntry = (guardEntry, .pending) := by decide
example : repairUnit (resolve minor copied .late) guardEntry =
    (.entry ⟨some (.source 0), some (.copied 0), .immediate 7⟩, .complete) := by decide
example : repairUnit (resolve minor .empty .late) guardEntry = (clear guardEntry, .complete) := by decide
example : repairUnit (resolve minor copied .late) payloadEntry =
    (.entry ⟨some (.source 0), some (.source 2), .reference (some (.copied 0))⟩, .complete) := by decide
example : repairUnit (resolve minor .empty .late) payloadEntry = (clear payloadEntry, .complete) := by decide

example : repairUnit (resolve minor .empty .late) (.field (.immediate 0)) =
    (.field (.immediate 0), .complete) := by decide
example : repairUnit (resolve minor .empty .late) (.field (.reference none)) =
    (.field (.reference none), .complete) := by decide
example : repairUnit (resolve minor .empty .late) (.field (.reference (some (.source 0)))) =
    (.field (.reference (some (.source 0))), .complete) := by decide
example : repairUnit (resolve minor .empty .late) (.field (.reference (some (.source 1)))) =
    (.field (.reference none), .complete) := by decide

end CloverGC.Generational.Weak.Examples
