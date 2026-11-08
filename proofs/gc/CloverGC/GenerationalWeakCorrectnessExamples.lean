import CloverGC.GenerationalWeakCorrectness

namespace CloverGC.Generational.Weak.CorrectnessExamples

open Copying

private def input : Input 5 6 := {
  heap := {
    data := {
      strong := {
        data := {
          graph := ⟨fun o => if o = 0 then [1] else if o = 1 then [2] else if o = 2 then [3] else []⟩
          objectKind := fun o => if o = 0 then .oldMovable .ordinary else .youngMovable .ordinary
          rememberedOwners := [0] }
        valid := by decide }
      units := {
        templates := fun id => if id = 0 then .field (.reference (some 1))
          else if id = 1 then .entry ⟨some 0, some 0, .reference (some 4)⟩
          else if id = 2 then .entry ⟨some 0, some 0, .immediate 7⟩
          else if id = 3 then .entry ⟨none, some 0, .immediate 17⟩
          else if id = 4 then .field (.reference (some 4))
          else .entry ⟨some 0, some 3, .reference (some 2)⟩
        ownerUnits := fun o => if o = 0 then [0, 1, 2, 3] else if o = 1 then [5] else []
        external := [4] }
      rememberedWeak := [2, 1, 0] }
    valid := by decide }
  collection := .young
  roots := [] }

private def before := (drainWeak input 5 (start input)).1
private def result := (collectWeak input).1
private def final := (Generational.evacuate input.strongInput).1.copying

example : before.strong.copying.pending = [] := beforeFinish_complete input
example : result.pending = [] := collection_weak_queue_empty input
example : result.registered = [4, 2, 1, 0, 5] := by decide
example : result.lateVisits = [4, 1, 0, 5] := by decide
-- A copied owner's payload relocates early, before its other guard is live.
example : before.units 5 =
    .entry ⟨some (.source 0), some (.source 3), .reference (some (.copied 1))⟩ := by decide
example : result.units 5 = desired input final 5 := collection_units_correct input 5 (by decide)
example : result.units 5 =
    .entry ⟨some (.source 0), some (.copied 2), .reference (some (.copied 1))⟩ := by decide
-- Late repair follows a live target, resets a dead entry, and clears a field.
example : result.units 0 = .field (.reference (some (.copied 0))) := by decide
example : result.units 1 = .entry ⟨none, none, .immediate 0⟩ := by decide
example : result.units 4 = .field (.reference none) := by decide
-- Conservative remembered unit 2 completes early and is not revisited.
example : result.units 2 = .entry ⟨some (.source 0), some (.source 0), .immediate 7⟩ := by decide
example : (2 : Fin 6) ∉ result.lateVisits := by decide
-- Unvisited inactive storage remains untouched, even though direct repair clears it.
example : result.units 3 = (input.heap.data.units.templates 3).initial :=
  collection_unregistered_unchanged input 3 (by decide)
example : result.units 3 ≠ desired input final 3 := by decide
-- In full mode that owner is scanned, so the inactive unit is registered and reset.
example : (collectWeak { input with collection := .full, roots := [0] }).1.units 3 =
    .entry ⟨none, none, .immediate 0⟩ := by decide

end CloverGC.Generational.Weak.CorrectnessExamples
