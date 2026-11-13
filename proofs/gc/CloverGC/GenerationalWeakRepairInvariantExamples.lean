import CloverGC.GenerationalWeakRepairInvariant

namespace CloverGC.Generational.Weak.InvariantExamples

open Copying

private def input : Input 3 3 := {
  heap := {
    data := {
      strong := {
        data := ⟨⟨fun _ => []⟩, fun o => if o = 0 then .oldMovable .ordinary
          else .youngMovable .ordinary, [0]⟩
        valid := by decide }
      units := {
        templates := fun id => if id = 0 then .field (.reference (some 1))
          else if id = 1 then .entry ⟨some 0, some 0, .immediate 7⟩
          else .entry ⟨some 0, some 2, .reference (some 1)⟩
        ownerUnits := fun owner => if owner = 0 then [0, 1, 2] else []
        external := [] } }
    valid := by decide }
  collection := .young
  roots := [] }

private def before : CopyState 3 := (discoverReference input.strongInput.copyInput .empty (.source 1)).1
private def final : CopyState 3 := (discoverReference input.strongInput.copyInput before (.source 2)).1
private def initial : Copying.Weak.State 3 3 :=
  ⟨⟨before, rawFields input.strongInput.copyInput, []⟩,
    fun id => (input.heap.data.units.templates id).initial, [], [], []⟩
private theorem initialInvariant : UnitInvariant input final initial :=
  ⟨by simp [initial], by simp [initial], by simp [initial]⟩
private theorem advanced : Progress initial.strong.copying final :=
  discovery_progress input.strongInput.copyInput before (.source 2)
private theorem batchInvariant : UnitInvariant input final (earlyVisits input initial [0, 2, 1]) :=
  earlyVisits_preserves input final initial [0, 2, 1] initialInvariant advanced (by decide)

example : UnitInvariant input final (earlyVisit input initial 0) :=
  earlyVisit_preserves input final initial 0 initialInvariant advanced (by decide)
example : UnitInvariant input final (earlyVisits input initial []) :=
  earlyVisits_preserves input final initial [] initialInvariant advanced (by decide)
example : (earlyVisits input initial [0, 2, 1]).registered = [0, 2, 1] := by decide
example : (earlyVisits input initial [0, 2, 1]).pending = [2] := by decide
-- Settled unit 0 is preserved while unit 2 defers and unit 1 completes.
example : (earlyVisits input initial [0, 2, 1]).units 0 = desired input final 0 :=
  batchInvariant.settled 0 (by decide) (by decide)
example : (earlyVisits input initial [0, 2, 1]).units 1 =
    .entry ⟨some (.source 0), some (.source 0), .immediate 7⟩ := by decide
-- Pending unit 2 partially relocates its payload, preserving its final result.
example : (earlyVisits input initial [0, 2, 1]).units 2 =
    .entry ⟨some (.source 0), some (.source 2), .reference (some (.copied 0))⟩ := by decide
example : Copying.Weak.repairUnit (resolve input.strongInput final .late)
    ((earlyVisits input initial [0, 2, 1]).units 2) =
    Copying.Weak.repairUnit (resolve input.strongInput final .late) (input.heap.data.units.templates 2).initial :=
  batchInvariant.compatible 2 (by decide)
-- An unregistered unit is still exactly its template.
example : (earlyVisits input initial [0, 2]).units 1 = (input.heap.data.units.templates 1).initial :=
  (earlyVisits_preserves input final initial [0, 2] initialInvariant advanced (by decide)).outside 1 (by decide)

end CloverGC.Generational.Weak.InvariantExamples
