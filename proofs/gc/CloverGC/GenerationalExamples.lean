import CloverGC.GenerationalCorrectness

namespace CloverGC.Generational.Examples

open Copying

private def nonmoving : RawGenerationalHeap 2 := {
  graph := ⟨fun o => if o = 0 then [1] else []⟩
  objectKind := fun o => if o = 0 then .nonmovingOld else .youngMovable .ordinary
  rememberedOwners := [0] }

example : WellFormed nonmoving := by decide
example : nonmoving.validate.isOk = true := by decide
example : ¬ WellFormed { nonmoving with rememberedOwners := [] } := by decide
example : ({ nonmoving with rememberedOwners := [] }.validate).isOk = false := by decide

private def raw : RawGenerationalHeap 2 := {
  graph := ⟨fun o => if o = 0 then [1] else []⟩
  objectKind := fun o => if o = 0 then .oldMovable .ordinary else .youngMovable .nonMoving
  rememberedOwners := [0] }

private def heap : GenerationalHeap 2 := ⟨raw, by decide⟩
private def minor : CollectionInput 2 := ⟨heap, .young, [0]⟩
private def fullGarbage : CollectionInput 2 := ⟨heap, .full, []⟩

example : ¬ WellFormed { raw with rememberedOwners := [] } := by decide
example : ({ raw with rememberedOwners := [] }.validate).isOk = false := by decide
example : ¬ WellFormed { raw with rememberedOwners := [0, 0] } := by decide
example : ¬ WellFormed { raw with rememberedOwners := [0, 1] } := by decide
example : WellFormed { raw with graph := ⟨fun _ => []⟩ } := by decide

example : (Generational.evacuate minor).1.roots = [.source 0] := by decide
example : (Generational.evacuate minor).1.originalFields 0 = [.copied 0] := by decide
example : (Generational.evacuate minor).1.copying.forwarding 0 = none := by decide
example : Reference.source 0 ∉ (Generational.evacuate minor).1.copying.marked := by decide
example : (Generational.evacuate minor).1.copying.copies = [⟨1, .nonMoving, []⟩] := by decide
example : (Generational.evacuate minor).2 = [.copied 0] := by decide
example : (Generational.evacuate fullGarbage).1.copying.copies = [] := by decide

-- Remembered owners retain their young children even without ordinary roots.
private def unrooted : CollectionInput 2 := { minor with roots := [] }

example : (Generational.evacuate unrooted).1.roots = [] ∧
    (Generational.evacuate unrooted).1.originalFields 0 = [.copied 0] ∧
    (Generational.evacuate unrooted).1.copying.copies = [⟨1, .nonMoving, []⟩] ∧
    (Generational.evacuate unrooted).2 = [.copied 0] := by decide

-- Duplicate roots and the remembered field share one copy and one scan.
private def aliased : CollectionInput 2 := { minor with roots := [1, 1] }

example : (Generational.evacuate aliased).1.roots = [.copied 0, .copied 0] ∧
    (Generational.evacuate aliased).1.originalFields 0 = [.copied 0] ∧
    (Generational.evacuate aliased).1.copying.copies = [⟨1, .nonMoving, []⟩] ∧
    (Generational.evacuate aliased).2 = [.copied 0] := by decide

end CloverGC.Generational.Examples
