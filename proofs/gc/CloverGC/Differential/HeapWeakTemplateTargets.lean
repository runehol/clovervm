import CloverGC.Differential.HeapGenerationalPreparation

namespace CloverGC.Differential

/-- Nullable guards contribute exactly the corresponding non-null target. -/
theorem ModelReference.toList {objects : Vector (Fin n) n} {before : Format.Reference} {after : Option (Fin n)}
    (projected : ModelReference objects before after) :
    ListProjection (fun id object => ModelReference objects (some id) (some object)) before.toList after.toList := by
  cases before <;> cases after
  · exact .nil
  · cases projected
  · cases projected
  · exact .cons projected .nil

/-- Tagged cells contribute their actual stored reference, or no target for an immediate. -/
theorem ModelCell.targets {objects : Vector (Fin n) n} {before : Format.Cell} {after : Copying.Weak.Cell (Fin n)}
    (projected : ModelCell objects before after) :
    ListProjection (fun id object => ModelReference objects (some id) (some object))
      (Format.WeakContents.targets (.field before)) after.target.toList := by
  cases before <;> cases after
  · exact .nil
  · cases projected
  · cases projected
  · exact ModelReference.toList projected

private theorem filter_targets_cons (head : Option α) (rest : List (Option α)) :
    (head :: rest).filterMap id = head.toList ++ rest.filterMap id := by
  cases head <;> rfl

private theorem field_targets (cell : Format.Cell) :
    Format.WeakContents.targets (.field cell) =
      (match cell with | .reference target => target.toList | .immediate _ => []) := by
  cases cell <;> rfl

/-- Target enumeration agrees in both directions and callback order, including
    the payload and the other guard of an inactive entry. -/
theorem ModelTemplate.targets {objects : Vector (Fin n) n} {before : Format.WeakContents}
    {after : Copying.Weak.Template n} (projected : ModelTemplate objects before after) :
    ListProjection (fun id object => ModelReference objects (some id) (some object))
      before.targets (after.targets.filterMap id) := by
  cases before with
  | field value =>
      cases after with
      | field cell =>
          simpa only [Copying.Weak.RepairUnit.targets, filter_targets_cons, List.filterMap_nil, List.append_nil]
            using ModelCell.targets projected
      | entry entry => cases projected
  | entry entry =>
      cases after with
      | field value => cases projected
      | entry output =>
          have first := ModelReference.toList projected.1
          have second := ModelReference.toList projected.2.1
          have payload := ModelCell.targets projected.2.2
          have all := (first.append second).append payload
          rw [field_targets] at all
          cases payloadShape : entry.payload <;>
            simpa only [payloadShape, Format.WeakContents.targets, Copying.Weak.RepairUnit.targets, filter_targets_cons,
              List.filterMap_nil, List.append_nil, List.append_assoc] using all

end CloverGC.Differential
