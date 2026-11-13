import CloverGC.GenerationalWeakHeap

namespace CloverGC.Generational.Weak.HeapExamples

open Copying.Weak

private def strong : GenerationalHeap 3 := {
  data := ⟨⟨fun _ => []⟩, fun owner => if owner = 0 then .oldMovable .ordinary
    else if owner = 1 then .youngMovable .ordinary else .nonmovingOld, [0]⟩
  valid := by decide }
private def raw : RawWeakHeap 3 3 := {
  strong := strong
  units := {
    templates := fun id => if id = 0 then .field (.reference (some 1))
      else if id = 1 then .entry ⟨some 0, some 2, .immediate 7⟩
      else .field (.reference (some 0))
    ownerUnits := fun owner => if owner = 0 then [0] else if owner = 2 then [1] else []
    external := [2] } }
private def heap : WeakHeap 3 3 := ⟨raw, by decide⟩
private def emptyRemembered : GenerationalHeap 3 :=
  ⟨{ strong.data with rememberedOwners := [] }, by decide⟩
private abbrev rejected (value : RawWeakHeap n k) : Prop :=
  ¬ WeakWellFormed value ∧ value.validate.isOk = false
private def setTemplate (value : RawWeakHeap n k) (id : Fin k) (unit : Template n) : RawWeakHeap n k :=
  { value with units := { value.units with templates := fun other =>
      if other = id then unit else value.units.templates other } }

example : raw.validate.isOk = true := by decide
-- A weak-only old-to-young store requires its OWNER in the shared set.
example : rejected { raw with strong := emptyRemembered } := by decide
example : WeakWellFormed { raw with strong := ⟨{ strong.data with rememberedOwners := [2, 0] }, by decide⟩ } := by decide
example : heap.data.strong.data.rememberedOwners = [0] := by decide
-- All three entry positions, even inactive entries, require owner coverage.
example : rejected (setTemplate raw 1 (.entry ⟨some 1, some 0, .immediate 7⟩)) := by decide
example : rejected (setTemplate raw 1 (.entry ⟨some 0, some 1, .immediate 7⟩)) := by decide
example : rejected (setTemplate raw 1 (.entry ⟨some 0, some 2, .reference (some 1)⟩)) := by decide
example : rejected (setTemplate raw 1 (.entry ⟨none, some 0, .reference (some 1)⟩)) := by decide
example : WeakWellFormed { setTemplate raw 0 (.field (.immediate 0)) with strong := emptyRemembered } := by decide
example : WeakWellFormed { setTemplate raw 0 (.field (.reference none)) with strong := emptyRemembered } := by decide
example : WeakWellFormed { raw with strong := emptyRemembered, units := { raw.units with
    ownerUnits := fun owner => if owner = 1 then [0] else if owner = 2 then [1] else [] } } := by decide
-- Missing locations and overlapping storage remain invalid.
example : rejected { raw with units := { raw.units with external := [] } } := by decide
example : rejected { raw with units := { raw.units with external := [2, 2] } } := by decide
example : rejected { raw with units := { raw.units with external := [0, 2] } } := by decide
example : rejected { raw with units := { raw.units with ownerUnits := fun owner =>
    if owner = 0 then [0, 0] else if owner = 2 then [1] else [] } } := by decide
example : rejected { raw with units := { raw.units with ownerUnits := fun owner =>
    if owner = 2 then [1] else [0] } } := by decide

private def emptyStrong : GenerationalHeap 0 := {
  data := ⟨⟨Fin.elim0⟩, Fin.elim0, []⟩
  valid := by decide }
example : (RawWeakHeap.mk emptyStrong ⟨Fin.elim0, Fin.elim0, []⟩).validate.isOk = true := by decide
example : (RawWeakHeap.mk emptyStrong
    ⟨fun _ : Fin 1 => .field (.reference none), Fin.elim0, [0]⟩).validate.isOk = true := by decide

end CloverGC.Generational.Weak.HeapExamples
