import CloverGC.GenerationalWeakHeap

namespace CloverGC.Generational.Weak.HeapExamples

open Copying.Weak

private def strong : GenerationalHeap 3 := {
  data := {
    graph := ⟨fun owner => if owner = 0 then [1] else []⟩
    objectKind := fun owner => if owner = 0 then .oldMovable .ordinary
      else if owner = 1 then .youngMovable .ordinary else .nonmovingOld
    rememberedOwners := [0] }
  valid := by decide }

private def raw : RawWeakHeap 3 3 := {
  strong := strong
  units := {
    templates := fun id => if id = 0 then .field (.reference (some 1))
      else if id = 1 then .entry ⟨some 0, some 2, .immediate 7⟩
      else .field (.reference (some 0))
    ownerUnits := fun owner => if owner = 0 then [0] else if owner = 2 then [1] else []
    external := [2] }
  rememberedWeak := [0] }

private def heap : WeakHeap 3 3 := ⟨raw, by decide⟩
private abbrev rejected (value : RawWeakHeap n k) : Prop :=
  ¬ WeakWellFormed value ∧ value.validate.isOk = false
private def setTemplate (value : RawWeakHeap n k) (id : Fin k) (unit : Template n) : RawWeakHeap n k :=
  { value with units := { value.units with templates := fun other =>
      if other = id then unit else value.units.templates other } }

example : raw.validate.isOk = true := by decide
example : heap.data.units.external = [2] := by decide
example : WeakWellFormed { raw with rememberedWeak := [1, 0] } := by decide
example : (match { raw with rememberedWeak := [1, 0] }.validate with
    | .ok value => value.data.rememberedWeak == [1, 0]
    | .error _ => false) = true := by decide

-- Strong remembered membership does not cover a weak field in the same owner.
example : rejected { raw with rememberedWeak := [] } := by decide
example : rejected { raw with rememberedWeak := [0, 0] } := by decide
example : rejected { raw with rememberedWeak := [0, 2] } := by decide

-- Each reference-bearing entry position independently requires weak coverage.
example : rejected (setTemplate raw 1 (.entry ⟨some 1, some 0, .immediate 7⟩)) := by decide
example : rejected (setTemplate raw 1 (.entry ⟨some 0, some 1, .immediate 7⟩)) := by decide
example : rejected (setTemplate raw 1 (.entry ⟨some 0, some 2, .reference (some 1)⟩)) := by decide
-- Inactive units still contain stored targets, including the payload.
example : rejected (setTemplate raw 1 (.entry ⟨none, some 1, .immediate 7⟩)) := by decide
example : rejected (setTemplate raw 1 (.entry ⟨some 1, none, .immediate 7⟩)) := by decide
example : rejected (setTemplate raw 1 (.entry ⟨none, some 0, .reference (some 1)⟩)) := by decide

-- The same young payload is valid once its nonmoving old owner's unit is remembered.
example : WeakWellFormed { setTemplate raw 1 (.entry ⟨some 0, some 2, .reference (some 1)⟩) with
    rememberedWeak := [1, 0] } := by decide
example : WeakWellFormed { setTemplate raw 0 (.field (.immediate 0)) with
    rememberedWeak := [] } := by decide
example : WeakWellFormed { setTemplate raw 0 (.field (.reference none)) with
    rememberedWeak := [] } := by decide

private def youngOwned : RawWeakHeap 3 3 := { raw with
  units := { raw.units with ownerUnits := fun owner =>
    if owner = 1 then [0] else if owner = 2 then [1] else [] }
  rememberedWeak := [] }

example : WeakWellFormed youngOwned := by decide
example : rejected { youngOwned with rememberedWeak := [0] } := by decide

-- Missing locations, repeated occurrences, and overlapping storage are invalid.
example : rejected { raw with units := { raw.units with external := [] } } := by decide
example : rejected { raw with units := { raw.units with ownerUnits := fun owner =>
    if owner = 2 then [1] else [] }, rememberedWeak := [] } := by decide
example : rejected { raw with units := { raw.units with external := [2, 2] } } := by decide
example : rejected { raw with units := { raw.units with external := [0, 2] } } := by decide
example : rejected { raw with units := { raw.units with ownerUnits := fun owner =>
    if owner = 0 then [0, 0] else if owner = 2 then [1] else [] } } := by decide
example : rejected { raw with units := { raw.units with ownerUnits := fun owner =>
    if owner = 2 then [1] else [0] } } := by decide

private def emptyStrong : GenerationalHeap 0 := {
  data := ⟨⟨Fin.elim0⟩, Fin.elim0, []⟩
  valid := by decide }

example : (RawWeakHeap.mk emptyStrong ⟨Fin.elim0, Fin.elim0, []⟩ []).validate.isOk = true := by decide
example : (RawWeakHeap.mk emptyStrong
    ⟨fun _ : Fin 1 => .field (.reference none), Fin.elim0, [0]⟩ []).validate.isOk = true := by decide

end CloverGC.Generational.Weak.HeapExamples
