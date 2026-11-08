import CloverGC.GenerationalWeakDrain

namespace CloverGC.Generational.Weak.DrainExamples

open Copying

private def input : Input 5 5 := {
  heap := {
    data := {
      strong := {
        data := {
          graph := ⟨fun o => if o = 0 then [1] else if o = 1 then [2] else []⟩
          objectKind := fun o => if o = 0 then .oldMovable .ordinary
            else if o = 3 then .nonmovingOld else .youngMovable .ordinary
          rememberedOwners := [0] }
        valid := by decide }
      units := {
        templates := fun id => if id = 0 then .field (.reference (some 2))
          else if id = 1 then .entry ⟨some 0, some 1, .reference (some 2)⟩
          else if id = 4 then .field (.reference (some 3)) else .field (.reference (some 4))
        ownerUnits := fun o => if o = 0 then [0, 4] else if o = 1 then [1]
          else if o = 3 then [3] else []
        external := [2] }
      rememberedWeak := [3, 0] }
    valid := by decide }
  collection := .young
  roots := [] }

private def drained := (drainWeak input 5 (start input)).1
private def final := (Generational.evacuate input.strongInput).1.copying
private theorem invariant : UnitInvariant input final drained := beforeFinish_units input

-- Strong progress preserves the copy installed by the remembered strong field.
example : final.forwarding 1 = some 0 :=
  (Generational.drain_progress input.strongInput 5 (Generational.start input.strongInput)).forwarding 1 0 (by decide)
example : Reference.copied 0 ∈ final.marked :=
  (Generational.drain_progress input.strongInput 5 (Generational.start input.strongInput)).marked (.copied 0) (by decide)
example : (Generational.discover input.strongInput final (.source 0)).1.forwarding 0 = none := by decide

-- Zero fuel retains the exact explicit registration prefix.
example : (drainWeak input 0 (start input)).1.registered = [2, 3, 0] := by decide
example : drained.registered = [2, 3, 0, 1] := by decide
example : drained.pending = [2, 3, 0] := by decide
-- The remembered old field remains deferred despite its target becoming live.
example : drained.units 0 = .field (.reference (some (.source 2))) := by decide
example : Copying.Weak.repairUnit (resolve input.strongInput final .late) (drained.units 0) =
    Copying.Weak.repairUnit (resolve input.strongInput final .late) (input.heap.data.units.templates 0).initial :=
  invariant.compatible 0 (by decide)
example : desired input final 0 = .field (.reference (some (.copied 1))) := by decide
-- The copied owner's unit completes after its strong children are discovered.
example : drained.units 1 = desired input final 1 := invariant.settled 1 (by decide) (by decide)
example : drained.units 4 = (input.heap.data.units.templates 4).initial := invariant.outside 4 (by decide)
example : desired input final 3 = .field (.reference none) := by decide

-- Full mode also establishes the invariant, registering old units by scanning.
example : UnitInvariant { input with collection := .full, roots := [0, 3] }
    (Generational.evacuate { input.strongInput with collection := .full, roots := [0, 3] }).1.copying
    (drainWeak { input with collection := .full, roots := [0, 3] } 5
      (start { input with collection := .full, roots := [0, 3] })).1 := beforeFinish_units _

end CloverGC.Generational.Weak.DrainExamples
