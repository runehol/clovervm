import CloverGC.GenerationalWeakDriverProofs

namespace CloverGC.Generational.Weak.DriverExamples

open Copying

private def strong : GenerationalHeap 5 := {
  data := {
    graph := ⟨fun owner => if owner = 0 then [1] else if owner = 1 then [4] else []⟩
    objectKind := fun owner => if owner = 0 then .oldMovable .ordinary
      else if owner = 3 then .nonmovingOld else .youngMovable .ordinary
    rememberedOwners := [3, 0] }
  valid := by decide }

private def heap : WeakHeap 5 7 := {
  data := {
    strong := strong
    units := {
      templates := fun id =>
        if id = 0 then .entry ⟨some 0, some 1, .reference (some 4)⟩
        else if id = 1 then .field (.reference (some 2))
        else if id = 2 then .field (.immediate 9)
        else if id = 3 then .field (.reference (some 3))
        else if id = 4 then .entry ⟨some 0, some 3, .reference (some 2)⟩
        else if id = 5 then .field (.reference (some 3))
        else .field (.immediate 17)
      ownerUnits := fun owner => if owner = 0 then [0, 5] else if owner = 3 then [1]
        else if owner = 4 then [3] else if owner = 2 then [6] else []
      external := [2, 4] } }
  valid := by decide }

private def minor : Input 5 7 := ⟨heap, .young, []⟩
private def full : Input 5 7 := ⟨heap, .full, [0, 3]⟩

-- External units precede roots and owner scans; owner strong slots precede weak units.
example : (start minor).registered = [2, 4, 1, 0, 5] := by decide
example : (start minor).pending = [4, 1, 0] := by decide
example : (start minor).units 0 = .entry ⟨some (.source 0), some (.copied 0), .reference (some (.source 4))⟩ := by decide
-- The strong remembered field discovers young 1, without scanning old 0.
example : (start minor).strong.copying.pending = [.copied 0] := by decide
example : (collectWeak minor).2 = [.copied 0, .copied 1] := by decide
example : (collectWeak minor).1.registered = [2, 4, 1, 0, 5, 3] := by decide
example : (collectWeak minor).1.lateVisits = [4, 1, 0] := by decide
example : (collectWeak minor).1.pending = [] := by decide

-- An unmarked old guard survives; both young references follow strong copies.
example : (collectWeak minor).1.units 0 =
    .entry ⟨some (.source 0), some (.copied 0), .reference (some (.copied 1))⟩ := by decide
-- Actual old-owner storage is repaired, including a nonmoving owner's field.
example : (collectWeak minor).1.units 1 = .field (.reference none) := by decide
example : (collectWeak minor).1.units 4 = .entry ⟨none, none, .immediate 0⟩ := by decide
example : (collectWeak minor).1.units 2 = .field (.immediate 9) := by decide
example : (collectWeak minor).1.units 3 = .field (.reference (some (.source 3))) := by decide
-- Old references remain unchanged; an unreachable young owner's unit is untouched.
example : (collectWeak minor).1.units 5 = (heap.data.units.templates 5).initial := by decide
example : (collectWeak minor).1.units 6 = .field (.immediate 17) := by decide

-- Full mode omits remembered registration; old owners are visited by scanning.
example : (start full).registered = [2, 4] := by decide
example : (collectWeak full).1.registered = [2, 4, 0, 5, 1, 3] := by decide
example : (collectWeak full).2 = [.copied 0, .source 3, .copied 1, .copied 2] := by decide
example : (collectWeak full).1.units 0 =
    .entry ⟨some (.copied 0), some (.copied 1), .reference (some (.copied 2))⟩ := by decide
example : (collectWeak full).1.units 1 = .field (.reference none) := by decide

-- Adding a strong root retains the formerly weak-only young target. Weak
-- external registration precedes roots; owned units are visited when their owners are scanned.
private def rooted : Input 5 7 := { minor with roots := [2] }
example : (start rooted).pending = [4, 0] := by decide
example : (collectWeak rooted).1.registered = [2, 4, 1, 0, 5, 6, 3] := by decide
example : (collectWeak rooted).1.units 1 = .field (.reference (some (.copied 0))) := by decide
example : (collectWeak rooted).1.units 4 =
    .entry ⟨some (.source 0), some (.source 3), .reference (some (.copied 0))⟩ := by decide

private def empty : Input 0 0 := {
  heap := {
    data := ⟨⟨⟨⟨Fin.elim0⟩, Fin.elim0, []⟩, by decide⟩, ⟨Fin.elim0, Fin.elim0, []⟩⟩
    valid := by decide }
  collection := .young
  roots := [] }

example : (collectWeak empty).2 = [] := by decide
example : (collectWeak empty).1.pending = [] := by decide

end CloverGC.Generational.Weak.DriverExamples
