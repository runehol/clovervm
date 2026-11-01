import CloverGC.EvacuationProofs

namespace CloverGC.Copying.EvacuationExamples

/-- A moves into NonMoving destination storage, C stays in place, and B moves
    into ordinary storage. D is unreachable even though it points into A. -/
def mixed : CopyInput 4 := {
  heap := ⟨fun object => match object.val with
    | 0 => [0, 2, 1, 1]
    | 1 => [2]
    | 2 => [0]
    | _ => [0]⟩
  movable := fun object => object.val != 2
  destination := fun object => if object.val = 0 then .nonMoving else .ordinary
}

def result := evacuate mixed [0, 0]

example : result.1.roots = [.copied 0, .copied 0] := by decide
example : result.2 = [.copied 0, .source 2, .copied 1] := by decide
example : result.1.copying.pending = [] := by decide
example : result.1.copying.copies.map CopyRecord.origin = [0, 1] := by decide
example : result.1.copying.copies.map CopyRecord.destination = [.nonMoving, .ordinary] := by decide
example : liveFields result.1 (.copied 0) = [.copied 0, .source 2, .copied 1, .copied 1] := by decide
example : liveFields result.1 (.copied 1) = [.source 2] := by decide
example : liveFields result.1 (.source 2) = [.copied 0] := by decide
example : result.1.copying.forwarding 3 = none := by decide
example : result.1.originalFields 0 = [.source 0, .source 2, .source 1, .source 1] := by decide

-- The unreachable D still points into the old source graph. The disposal
-- theorem deliberately covers surviving data, not all original storage.
example : result.1.originalFields 3 = [.source 0] := by decide

-- Copying root A alone does not establish disposal safety: A-prime still has
-- raw children before the queue is drained. The scan repairs those exact slots.
example : liveFields (seedRoots mixed [0]) (.copied 0) =
    [.source 0, .source 2, .source 1, .source 1] := by decide

-- Self-references and children newly allocated while scanning must survive
-- writeback to the latest copy table, not a pre-discovery snapshot.
example : (scanCopy mixed (seedRoots mixed [0])).map (fun pair => pair.2.copying.copies.length) =
    some 2 := by decide

def emptyInput : CopyInput 0 := ⟨⟨Fin.elim0⟩, Fin.elim0, Fin.elim0⟩

example : (evacuate emptyInput []).2 = [] := by decide
example : (evacuate mixed []).1.copying.copies = [] := by decide
example : (evacuate mixed []).1.copying.pending = [] := by decide

def chain : CopyInput 8 := {
  heap := ⟨fun object => if h : object.val + 1 < 8 then [⟨object.val + 1, h⟩] else []⟩
  movable := fun _ => true
  destination := fun _ => .ordinary
}

example : (evacuate chain [0]).2 =
    [.copied 0, .copied 1, .copied 2, .copied 3, .copied 4, .copied 5, .copied 6, .copied 7] := by decide
example : (evacuate chain [0]).1.copying.pending = [] := by decide
example : (drainCopies chain 7 (seedRoots chain [0])).1.copying.pending = [.copied 7] := by decide

def allFixed : CopyInput 2 := ⟨⟨fun object => if object.val = 0 then [1] else [0]⟩,
  fun _ => false, fun _ => .ordinary⟩

example : (evacuate allFixed [0, 0]).2 = [.source 0, .source 1] := by decide
example : (evacuate allFixed [0]).1.copying.copies = [] := by decide

end CloverGC.Copying.EvacuationExamples
