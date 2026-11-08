import CloverGC.GenerationalHeap

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

example : ¬ WellFormed { raw with rememberedOwners := [] } := by decide
example : ({ raw with rememberedOwners := [] }.validate).isOk = false := by decide
example : ¬ WellFormed { raw with rememberedOwners := [0, 0] } := by decide
example : ¬ WellFormed { raw with rememberedOwners := [0, 1] } := by decide
example : WellFormed { raw with graph := ⟨fun _ => []⟩ } := by decide

end CloverGC.Generational.Examples
