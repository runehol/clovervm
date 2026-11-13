import CloverGC.GenerationalWeakSafety

namespace CloverGC.Generational.Weak.SafetyExamples

open Copying

private def input : Input 5 6 := {
  heap := {
    data := {
      strong := {
        data := {
          graph := ⟨fun o => if o = 0 then [1] else []⟩
          objectKind := fun o => if o = 0 || o = 4 then .oldMovable .ordinary
            else if o = 3 then .nonmovingOld else .youngMovable .ordinary
          rememberedOwners := [0, 3] }
        valid := by decide }
      units := {
        templates := fun id => if id = 0 then .field (.reference (some 0))
          else if id = 1 then .entry ⟨none, some 3, .reference (some 0)⟩
          else if id = 2 then .field (.reference (some 1))
          else if id = 3 then .field (.reference (some 2))
          else if id = 4 then .entry ⟨some 0, some 3, .immediate 7⟩
          else .entry ⟨some 0, some 1, .reference (some 1)⟩
        ownerUnits := fun o => if o = 4 then [0, 1] else if o = 0 then [2] else if o = 3 then [3] else if o = 1 then [5] else []
        external := [4] } }
    valid := by decide }
  collection := .young
  roots := [] }

private def result := (collectWeak input).1

-- Neither old target needs a mark in young mode, including the movable one.
example : Reference.source 0 ∉ result.strong.copying.marked := by decide
example : Reference.source 3 ∉ result.strong.copying.marked := by decide
example : Safe input.strongInput result.strong.copying (.source 0) :=
  collection_retained_owner_safe input 4 (Or.inl (by decide)) 0 (by decide) (.source 0) (by decide)
example : Safe input.strongInput result.strong.copying (.source 3) :=
  (collection_disposal_safe input).1 4 (by decide) (.source 3) (by decide)
-- Unremembered inactive storage still holds old references; coverage makes them safe.
example : (1 : Fin 6) ∉ result.registered := by decide
example : result.units 1 = .entry ⟨none, some (.source 3), .reference (some (.source 0))⟩ := by decide
example : Safe input.strongInput result.strong.copying (.source 0) :=
  (collection_disposal_safe input).2 4 (Or.inl (by decide)) 1 (by decide) (.source 0) (by decide)
-- Remembered and copied-owner units retain only actual allocated young copies.
example : result.units 2 = .field (.reference (some (.copied 0))) := by decide
example : Safe input.strongInput result.strong.copying (.copied 0) :=
  collection_registered_safe input 2 (by decide) (.copied 0) (by decide)
example : result.units 5 =
    .entry ⟨some (.source 0), some (.copied 0), .reference (some (.copied 0))⟩ := by decide
example : result.units 3 = .field (.reference none) := by decide

-- Full mode clears the external entry's unreachable old guards. Unreachable
-- owners' diagnostic fields may still hold stale source references, as intended.
private def full : Input 5 6 := { input with collection := .full, roots := [1] }
example : (collectWeak full).1.units 4 = .entry ⟨none, none, .immediate 0⟩ := by decide
example : (2 : Fin 6) ∉ (collectWeak full).1.registered := by decide
example : (collectWeak full).1.units 2 = .field (.reference (some (.source 1))) := by decide
example : ¬ Safe full.strongInput (collectWeak full).1.strong.copying (.source 1) := by
  change ¬ (true = false ∨ true = false)
  decide

end CloverGC.Generational.Weak.SafetyExamples
