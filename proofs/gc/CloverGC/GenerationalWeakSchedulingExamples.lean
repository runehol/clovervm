import CloverGC.GenerationalWeakScheduling

namespace CloverGC.Generational.Weak.SchedulingExamples

open Copying

private def minor : Input 2 2 := {
  heap := {
    data := {
      strong := {
        data := ⟨⟨fun _ => []⟩, fun o => if o = 0 then .oldMovable .ordinary
          else .youngMovable .ordinary, [0]⟩
        valid := by decide }
      units := {
        templates := fun id => if id = 0 then .field (.reference (some 1)) else .field (.immediate 4)
        ownerUnits := fun owner => if owner = 0 then [0] else []
        external := [1] } }
    valid := by decide }
  collection := .young
  roots := [] }

-- An unrooted old owner's remembered unit is still registered and repaired.
example : (collectWeak minor).1.registered = [1, 0] := by decide
example : (collectWeak minor).1.registered.Nodup := collection_registered_nodup minor
example : (collectWeak minor).2 = [] := by decide
example : (collectWeak minor).1.lateVisits = [0] := by decide
example : (collectWeak minor).1.lateVisits.Nodup := collection_late_nodup minor
example : (collectWeak minor).1.units 0 = .field (.reference none) := by decide

-- Full mode registers that unit only if its owner is strongly reached.
example : (collectWeak { minor with collection := .full }).1.registered = [1] := by decide
example : (collectWeak { minor with collection := .full }).1.lateVisits = [] := by decide
example : (collectWeak { minor with collection := .full, roots := [0] }).1.registered = [1, 0] := by decide
example : (collectWeak { minor with collection := .full, roots := [0] }).1.lateVisits = [0] := by decide

end CloverGC.Generational.Weak.SchedulingExamples
